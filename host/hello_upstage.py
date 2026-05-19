"""Smoke test for Upstage Solar Pro 3 (OpenAI-compatible API).

Usage:
    MODE=live   python host/hello_upstage.py   # real API call (requires UPSTAGE_API_KEY)
    MODE=mock   python host/hello_upstage.py   # dummy response, no network
    (default MODE is read from .env or environment; falls back to "mock")

Emits a single-line JSON to stdout so the harness can parse it.
"""
from __future__ import annotations

import json
import os
import sys
import time
from typing import Any


def _load_dotenv_if_present() -> None:
    try:
        from dotenv import load_dotenv  # type: ignore[import-not-found]
    except ImportError:
        return
    load_dotenv()


def _mock_reply(prompt: str) -> str:
    time.sleep(0.05)
    return f"[mock] echo: {prompt[:60]}"


def _live_reply(system: str, user: str) -> str:
    from openai import OpenAI  # type: ignore[import-not-found]

    api_key = os.environ.get("UPSTAGE_API_KEY")
    if not api_key:
        raise RuntimeError("UPSTAGE_API_KEY missing — set it in .env for live mode")
    client = OpenAI(api_key=api_key, base_url="https://api.upstage.ai/v1")
    response = client.chat.completions.create(
        model="solar-pro",
        max_tokens=64,
        messages=[
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
    )
    return response.choices[0].message.content or ""


def main() -> int:
    _load_dotenv_if_present()
    mode = os.environ.get("MODE", "mock").lower()
    prompt = "Say 'hello, liberal_os' in exactly five words."
    system = "You are a terse assistant."

    started = time.time()
    try:
        if mode == "live":
            reply = _live_reply(system, prompt)
        elif mode in ("mock", "replay"):
            reply = _mock_reply(prompt)
        else:
            raise ValueError(f"unknown MODE={mode!r}; expected live|mock|replay")
    except Exception as exc:  # noqa: BLE001 — surfacing as JSON to harness
        out: dict[str, Any] = {
            "ok": False,
            "mode": mode,
            "error": str(exc),
            "elapsed_s": round(time.time() - started, 3),
        }
        print(json.dumps(out))
        return 1

    out = {
        "ok": True,
        "mode": mode,
        "reply": reply,
        "elapsed_s": round(time.time() - started, 3),
    }
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
