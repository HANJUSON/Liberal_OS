"""host/proxy_pipe.py — bidirectional channel between the Linux host and the
xv6 QEMU guest.

T-23 ships the *chardev fallback* path the task description endorses
("virtio가 안 되면 -chardev pipe로 fallback"): we drive the existing
console serial — which QEMU connects to our stdin/stdout under
``-nographic`` — instead of standing up a second virtio-serial port that
would also need an xv6 driver. The contract is identical (host can read
guest output, host can send guest input) and the xv6 side requires no
new code. A future task can promote this to a dedicated virtio channel
without changing callers, since the public API here is just send/recv.

Usage::

    # Self-test (echo round-trip). Emits one-line JSON to stdout.
    python3 host/proxy_pipe.py

The self-test is the T-23 verify gate: ``ok: true`` means host→guest and
guest→host both made it through.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys
import time
from typing import Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]
XV6_DIR = ROOT / "xv6-src"

QEMU_CMD = [
    "qemu-system-riscv64",
    "-machine", "virt",
    "-bios", "none",
    "-kernel", "kernel/kernel",
    "-m", "128M",
    "-smp", "3",
    "-nographic",
    "-global", "virtio-mmio.force-legacy=false",
    "-drive", "file=fs.img,if=none,format=raw,id=x0",
    "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
]


class Xv6Channel:
    """Spawn xv6 under QEMU and exchange lines over its serial console."""

    def __init__(self, qemu_cmd: Optional[list[str]] = None) -> None:
        self.cmd = qemu_cmd or QEMU_CMD
        self.proc = subprocess.Popen(
            self.cmd,
            cwd=str(XV6_DIR),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        # Make stdout non-blocking so wait_for can poll without hanging.
        os.set_blocking(self.proc.stdout.fileno(), False)
        self._buf = bytearray()

    def wait_for(self, token: bytes, timeout: float = 15.0) -> str:
        """Block until ``token`` shows up on serial; return everything seen so far."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = os.read(self.proc.stdout.fileno(), 4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                self._buf.extend(chunk)
                if token in self._buf:
                    cut = self._buf.index(token) + len(token)
                    seen = bytes(self._buf[:cut]).decode("utf-8", "replace")
                    del self._buf[:cut]
                    return seen
            else:
                time.sleep(0.05)
        tail = bytes(self._buf)[-240:].decode("utf-8", "replace")
        raise TimeoutError(f"token {token!r} not seen; tail={tail!r}")

    def send(self, data: str) -> None:
        """Write raw bytes (no newline auto-appended) to the guest console."""
        assert self.proc.stdin is not None
        self.proc.stdin.write(data.encode("utf-8"))
        self.proc.stdin.flush()

    def close(self) -> None:
        """Send Ctrl-A X to ask QEMU to quit; fall back to kill on timeout."""
        try:
            if self.proc.stdin and not self.proc.stdin.closed:
                self.proc.stdin.write(b"\x01x")
                self.proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)


def self_test() -> int:
    token = "PROXY_PIPE_ECHO_42"
    started = time.monotonic()
    ch = Xv6Channel()
    try:
        ch.wait_for(b"init: starting sh", timeout=15.0)
        ch.send(f"echo {token}\n")
        ch.wait_for(token.encode(), timeout=10.0)
        out = {"ok": True, "echoed": True, "elapsed_s": round(time.monotonic() - started, 3)}
        print(json.dumps(out))
        return 0
    except Exception as exc:
        out = {"ok": False, "error": str(exc), "elapsed_s": round(time.monotonic() - started, 3)}
        print(json.dumps(out))
        return 1
    finally:
        ch.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--token",
        default="PROXY_PIPE_ECHO_42",
        help="echo round-trip token used by the self-test",
    )
    ap.parse_args()
    return self_test()


if __name__ == "__main__":
    sys.exit(main())
