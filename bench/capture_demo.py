"""bench/capture_demo.py — capture a demo session as plain text.

Spawns xv6, sends a scripted sequence of commands, and records the
full serial transcript to stdout. Intended use:

    python3 bench/capture_demo.py > docs/demo_transcript.txt

Mode is always `mock` because demo capture must work without an
Upstage key and without network. The transcript shows:

  1. `agentstat`        before any triage — baseline proc table
  2. `triage short.log` — five-agent pipeline end-to-end
  3. `agentstat`        right after triage — shows zombies/exit
  4. `priotest`         — priority scheduling effect (6 children)

This is a text-only stand-in for `docs/demo.gif` (T-73, human work).
"""
from __future__ import annotations

import os
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "host"))

from proxy_daemon import HANDLERS, PROXY_REQ_TAG, PROXY_RES_TAG  # noqa: E402
from proxy_pipe import Xv6Channel  # noqa: E402


def _drive_and_capture(commands: list[str], end_token: str,
                       per_cmd_timeout: float = 25.0) -> str:
    """Send each command, service every PROXY_REQ along the way, return
    the full transcript (everything xv6 emitted from boot to end_token)."""
    handler = HANDLERS["mock"]
    ch = Xv6Channel()
    transcript = []
    try:
        boot = ch.wait_for(b"init: starting sh", timeout=15.0)
        transcript.append(boot)
        time.sleep(0.2)

        for cmd in commands:
            transcript.append(f"\n$ {cmd}\n")
            ch.send(cmd + "\n")
            deadline = time.monotonic() + per_cmd_timeout
            line = bytearray()
            # Collect until shell prompt returns ("$ ") or end token, or timeout.
            while time.monotonic() < deadline:
                try:
                    chunk = os.read(ch.proc.stdout.fileno(), 4096)
                except BlockingIOError:
                    chunk = b""
                if not chunk:
                    time.sleep(0.02)
                    continue
                for byte in chunk:
                    line.append(byte)
                    if byte == 0x0A:
                        text = bytes(line).decode("utf-8", "replace")
                        line.clear()
                        transcript.append(text)
                        # Service any in-flight PROXY_REQ frame.
                        stripped = text.rstrip("\n")
                        if stripped.startswith(PROXY_REQ_TAG + "\t"):
                            parts = stripped.split("\t", 3)
                            if len(parts) == 4:
                                _, req_id, role, prompt = parts
                                reply = handler(role, prompt)
                                ch.send(f"{PROXY_RES_TAG}\t{req_id}\t{reply}\n")
                        if end_token and end_token in text:
                            deadline = 0  # exit outer loop on next iter
                            break
                # Detect shell prompt return as a softer stop signal.
                tail = bytes(line)
                if tail.endswith(b"$ "):
                    transcript.append(tail.decode("utf-8", "replace"))
                    line.clear()
                    break
                if deadline == 0:
                    break
    finally:
        ch.close()
    return "".join(transcript)


def main() -> int:
    commands = [
        "agentstat",
        "triage short.log",
        "agentstat",
        "priotest",
    ]
    out = _drive_and_capture(commands, end_token="PRIOTEST_DONE")
    sys.stdout.write(out)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
