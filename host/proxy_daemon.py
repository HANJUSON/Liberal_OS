"""host/proxy_daemon.py — Liberal_OS host-side proxy daemon.

Phase 2 milestone, three modes:

* ``mock``   — pure echo, no network. Default. Used by autotest. (T-25)
* ``live``   — real Upstage Solar Pro 3 call via the OpenAI-compatible
               SDK; caches every successful response on disk so a
               subsequent live or replay run hits cache. (T-26)
* ``replay`` — cache-only. Cache miss raises so eval runs can't silently
               drift onto a fresh API call. (T-27)

Frame format on the xv6 console serial (matches user/proxy_client.h):

    PROXY_REQ\\t<id>\\t<role>\\t<prompt>\\n     (xv6 → host)
    PROXY_RES\\t<id>\\t<result>\\n              (host → xv6)

The <id> field — currently the caller's pid — is opaque to the host
beyond round-tripping it back unchanged; it lets multiple sibling
agents in a pipe chain share the console without grabbing each other's
responses.

Self-test (``--self-test``) spawns xv6, drives ``proxytest <role>
<prompt>``, services the resulting PROXY_REQ, and waits for the guest
to print PROXY_TEST_OK / PROXY_TEST_DIFF. The mock self-test is the
T-25 verify gate; replay self-test exercises cache hit/miss semantics
without ever touching the network and so doubles as the T-27 verify.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import sys
import time
from typing import Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "host"))

from proxy_pipe import Xv6Channel  # noqa: E402  reuse T-23 transport

PROXY_REQ_TAG = "PROXY_REQ"
PROXY_RES_TAG = "PROXY_RES"
CACHE_DIR = ROOT / ".cache" / "llm"


def _cache_key(role: str, prompt: str) -> str:
    return hashlib.sha256(f"{role}|{prompt}".encode("utf-8")).hexdigest()


def cache_get(role: str, prompt: str) -> Optional[str]:
    path = CACHE_DIR / f"{_cache_key(role, prompt)}.json"
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text())["result"]
    except (json.JSONDecodeError, KeyError):
        return None


def cache_put(role: str, prompt: str, result: str) -> None:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    path = CACHE_DIR / f"{_cache_key(role, prompt)}.json"
    payload = {
        "role": role,
        "prompt": prompt,
        "result": result,
        "ts": time.time(),
    }
    path.write_text(json.dumps(payload, ensure_ascii=False))


class ReplayMiss(RuntimeError):
    """Replay mode encountered a request with no cached response."""


def mock_handler(role: str, prompt: str) -> str:
    return prompt  # pure echo — matches proxytest.c equality check.


def live_handler(role: str, prompt: str) -> str:
    cached = cache_get(role, prompt)
    if cached is not None:
        return cached
    api_key = os.environ.get("UPSTAGE_API_KEY")
    if not api_key:
        raise RuntimeError("UPSTAGE_API_KEY missing — set it in .env for live mode")
    from openai import OpenAI  # type: ignore[import-not-found]
    client = OpenAI(api_key=api_key, base_url="https://api.upstage.ai/v1")
    response = client.chat.completions.create(
        model="solar-pro",
        max_tokens=256,
        messages=[
            {"role": "system", "content": f"You are the Liberal_OS {role} agent."},
            {"role": "user", "content": prompt},
        ],
    )
    result = response.choices[0].message.content or ""
    cache_put(role, prompt, result)
    return result


def replay_handler(role: str, prompt: str) -> str:
    cached = cache_get(role, prompt)
    if cached is None:
        raise ReplayMiss(f"no cache for role={role!r} prompt={prompt!r}")
    return cached


HANDLERS = {
    "mock": mock_handler,
    "live": live_handler,
    "replay": replay_handler,
}


def _load_dotenv_if_present() -> None:
    try:
        from dotenv import load_dotenv  # type: ignore[import-not-found]
    except ImportError:
        return
    load_dotenv()


def _drive_proxytest(ch: Xv6Channel, mode: str, role: str, prompt: str,
                     timeout_s: float) -> dict[str, object]:
    """Spawn proxytest in xv6, service one PROXY_REQ, return self-test result."""
    started = time.monotonic()
    handler = HANDLERS[mode]

    ch.send(f"proxytest {role} {prompt}\n")

    deadline = time.monotonic() + timeout_s
    line = bytearray()
    served = 0
    last_error: Optional[str] = None

    while time.monotonic() < deadline:
        try:
            chunk = os.read(ch.proc.stdout.fileno(), 4096)
        except BlockingIOError:
            chunk = b""
        if not chunk:
            time.sleep(0.05)
            continue
        for byte in chunk:
            if byte != 0x0A:
                line.append(byte)
                continue
            text = bytes(line).decode("utf-8", "replace")
            line.clear()
            if text.startswith(PROXY_REQ_TAG + "\t"):
                parts = text.split("\t", 3)
                if len(parts) != 4:
                    continue
                _, req_id, got_role, got_prompt = parts
                try:
                    reply = handler(got_role, got_prompt)
                    ch.send(f"{PROXY_RES_TAG}\t{req_id}\t{reply}\n")
                    served += 1
                except Exception as exc:  # noqa: BLE001
                    last_error = f"{type(exc).__name__}: {exc}"
                    # Send a sentinel so the guest doesn't hang waiting.
                    ch.send(f"{PROXY_RES_TAG}\t{req_id}\t__HANDLER_ERROR__\n")
            elif "PROXY_TEST_OK" in text:
                return {
                    "ok": True,
                    "mode": mode,
                    "served": served,
                    "elapsed_s": round(time.monotonic() - started, 3),
                }
            elif "PROXY_TEST_DIFF" in text:
                return {
                    "ok": False,
                    "mode": mode,
                    "served": served,
                    "reason": "response differs from prompt (expected for live mode)",
                    "last_error": last_error,
                    "elapsed_s": round(time.monotonic() - started, 3),
                }
            elif "PROXY_TEST_FAIL" in text:
                return {
                    "ok": False,
                    "mode": mode,
                    "served": served,
                    "reason": text,
                    "last_error": last_error,
                    "elapsed_s": round(time.monotonic() - started, 3),
                }

    return {
        "ok": False,
        "mode": mode,
        "served": served,
        "reason": "timeout",
        "last_error": last_error,
        "elapsed_s": round(time.monotonic() - started, 3),
    }


def _drive_triage(ch: Xv6Channel, mode: str, input_file: str,
                  timeout_s: float) -> dict[str, object]:
    """Run `triage <input_file>` in xv6 and service every PROXY_REQ until
    the evaluator emits TRIAGE_DONE (or we time out)."""
    started = time.monotonic()
    handler = HANDLERS[mode]

    ch.send(f"triage {input_file}\n")

    deadline = time.monotonic() + timeout_s
    line = bytearray()
    served_by_role: dict[str, int] = {}
    evaluator_outputs: list[str] = []
    last_error: Optional[str] = None
    eval_retries = 0
    eval_fails = 0
    eval_oks = 0

    while time.monotonic() < deadline:
        try:
            chunk = os.read(ch.proc.stdout.fileno(), 4096)
        except BlockingIOError:
            chunk = b""
        if not chunk:
            time.sleep(0.02)
            continue
        for byte in chunk:
            if byte != 0x0A:
                line.append(byte)
                continue
            text = bytes(line).decode("utf-8", "replace")
            line.clear()
            if text.startswith(PROXY_REQ_TAG + "\t"):
                parts = text.split("\t", 3)
                if len(parts) != 4:
                    continue
                _, req_id, role, prompt = parts
                try:
                    reply = handler(role, prompt)
                    ch.send(f"{PROXY_RES_TAG}\t{req_id}\t{reply}\n")
                    served_by_role[role] = served_by_role.get(role, 0) + 1
                except Exception as exc:  # noqa: BLE001
                    last_error = f"{type(exc).__name__}: {exc}"
                    ch.send(f"{PROXY_RES_TAG}\t{req_id}\t__HANDLER_ERROR__\n")
            elif text.startswith("EVAL_RETRY"):
                eval_retries += 1
            elif text.startswith("evaluator:FAIL:"):
                eval_fails += 1
                evaluator_outputs.append(text)
            elif text.startswith("evaluator:OK:"):
                eval_oks += 1
                evaluator_outputs.append(text)
            elif text.startswith("evaluator:"):
                evaluator_outputs.append(text)
            elif text == "TRIAGE_DONE":
                roles_seen = sorted(served_by_role)
                expected = ["classifier", "evaluator", "fixsuggest", "parser", "rootcause"]
                ok = roles_seen == expected and len(evaluator_outputs) > 0
                return {
                    "ok": ok,
                    "mode": mode,
                    "served": served_by_role,
                    "evaluator_lines": len(evaluator_outputs),
                    "evaluator_sample": evaluator_outputs[:3],
                    "eval_retries": eval_retries,
                    "eval_fails": eval_fails,
                    "eval_oks": eval_oks,
                    "missing_roles": [r for r in expected if r not in served_by_role],
                    "elapsed_s": round(time.monotonic() - started, 3),
                }

    return {
        "ok": False,
        "mode": mode,
        "served": served_by_role,
        "evaluator_lines": len(evaluator_outputs),
        "reason": "timeout",
        "last_error": last_error,
        "elapsed_s": round(time.monotonic() - started, 3),
    }


def run_self_test(mode: str, role: str, prompt: str, timeout_s: float) -> int:
    _load_dotenv_if_present()
    ch = Xv6Channel()
    try:
        ch.wait_for(b"init: starting sh", timeout=15.0)
        # Give sh a beat to install its console handlers before stuffing input.
        time.sleep(0.2)
        result = _drive_proxytest(ch, mode, role, prompt, timeout_s)
        print(json.dumps(result, ensure_ascii=False))
        return 0 if result.get("ok") else 1
    finally:
        ch.close()


def run_triage(mode: str, input_file: str, timeout_s: float) -> int:
    _load_dotenv_if_present()
    ch = Xv6Channel()
    try:
        ch.wait_for(b"init: starting sh", timeout=15.0)
        time.sleep(0.2)
        result = _drive_triage(ch, mode, input_file, timeout_s)
        print(json.dumps(result, ensure_ascii=False))
        return 0 if result.get("ok") else 1
    finally:
        ch.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=("mock", "live", "replay"), default="mock")
    ap.add_argument("--role", default="echo")
    ap.add_argument("--prompt", default="hello")
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--self-test", action="store_true",
                    help="spawn xv6, drive proxytest, verify the round trip")
    ap.add_argument("--triage", metavar="INPUT",
                    help="spawn xv6, run `triage <INPUT>`, service the 5-stage pipeline")
    args = ap.parse_args()

    if args.triage:
        return run_triage(args.mode, args.triage, args.timeout)
    return run_self_test(args.mode, args.role, args.prompt, args.timeout)


if __name__ == "__main__":
    sys.exit(main())
