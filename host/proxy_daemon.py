"""host/proxy_daemon.py — Liberal_OS host-side proxy daemon.

T-25 ships the **mock mode** of this daemon: spawn xv6 under QEMU,
listen on its console serial for PROXY_REQ frames emitted by user
programs that include ``user/proxy_client.h`` (T-24), and reply with
PROXY_RES frames. Mock mode is pure echo — it does not call any LLM —
which keeps autotest hermetic.

Frame format (must match user/proxy_client.h):

    PROXY_REQ\\t<role>\\t<prompt>\\n     (xv6 → host)
    PROXY_RES\\t<result>\\n              (host → xv6)

Modes (--mode):

    mock     — echo prompt as result (default; T-25)
    live     — real Upstage Solar Pro 3 call (T-26)
    replay   — disk-backed cache of prior live responses (T-27)

The self-test (``--self-test``) spawns xv6, drives ``proxytest hello``
in the guest shell, services the resulting PROXY_REQ, waits for
PROXY_TEST_OK, and emits one-line JSON. This is the T-24/T-25 verify
gate.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "host"))

from proxy_pipe import Xv6Channel  # noqa: E402  reuse the T-23 transport


PROXY_REQ_TAG = "PROXY_REQ"
PROXY_RES_TAG = "PROXY_RES"


def mock_handler(role: str, prompt: str) -> str:
    """Echo the prompt unchanged; role is logged via the caller."""
    return prompt


def run_self_test(timeout_s: float = 20.0) -> int:
    started = time.monotonic()
    ch = Xv6Channel()
    log_lines: list[str] = []
    try:
        ch.wait_for(b"init: starting sh", timeout=15.0)

        # Drive a request from the guest. proxytest writes its own
        # PROXY_REQ via proxy_client.h; the shell prompt then waits.
        ch.send("proxytest hello\n")

        # Service the channel until we either see PROXY_TEST_OK or time out.
        deadline = time.monotonic() + timeout_s
        line = bytearray()
        served = 0
        while time.monotonic() < deadline:
            try:
                chunk = os.read(ch.proc.stdout.fileno(), 4096)
            except BlockingIOError:
                chunk = b""
            if not chunk:
                time.sleep(0.05)
                continue
            for byte in chunk:
                if byte == 0x0A:  # '\n'
                    text = bytes(line).decode("utf-8", "replace")
                    log_lines.append(text)
                    line.clear()
                    if text.startswith(PROXY_REQ_TAG + "\t"):
                        parts = text.split("\t", 2)
                        if len(parts) == 3:
                            _, role, prompt = parts
                            reply = mock_handler(role, prompt)
                            ch.send(f"{PROXY_RES_TAG}\t{reply}\n")
                            served += 1
                    elif "PROXY_TEST_OK" in text:
                        out = {
                            "ok": True,
                            "mode": "mock",
                            "served": served,
                            "elapsed_s": round(time.monotonic() - started, 3),
                        }
                        print(json.dumps(out))
                        return 0
                    elif "PROXY_TEST_FAIL" in text or "PROXY_TEST_DIFF" in text:
                        out = {
                            "ok": False,
                            "mode": "mock",
                            "served": served,
                            "reason": text,
                            "elapsed_s": round(time.monotonic() - started, 3),
                        }
                        print(json.dumps(out))
                        return 1
                else:
                    line.append(byte)
        out = {
            "ok": False,
            "mode": "mock",
            "served": served,
            "reason": "timeout",
            "tail": log_lines[-10:],
            "elapsed_s": round(time.monotonic() - started, 3),
        }
        print(json.dumps(out))
        return 1
    finally:
        ch.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=("mock", "live", "replay"), default="mock")
    ap.add_argument("--self-test", action="store_true",
                    help="spawn xv6, drive proxytest, verify echo round-trip")
    args = ap.parse_args()

    if args.mode != "mock":
        # live/replay arrive in T-26 / T-27; fail loudly until then.
        print(json.dumps({"ok": False, "error": f"mode {args.mode!r} not implemented yet"}))
        return 2

    # Default action when invoked: run the self-test. (A standalone listen
    # loop driven by an external orchestrator can be added in a later task.)
    if args.self_test or True:
        return run_self_test()


if __name__ == "__main__":
    sys.exit(main())
