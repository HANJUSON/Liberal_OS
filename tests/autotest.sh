#!/usr/bin/env bash
# tests/autotest.sh — headless xv6 smoke gate.
#
# Boots xv6 under QEMU with the smoke input file fed on stdin, captures
# the serial console, and PASS/FAIL based on grepping the SMOKE_TEST_PASS
# token printed by user/smoketest.c. Designed to finish well under 60s.
#
# Env knobs:
#   AUTOTEST_TIMEOUT  — qemu wallclock budget in seconds (default 45)
#   AUTOTEST_CLEAN=1  — force `make clean` before build (use after .h changes)
#
# Exits 0 on PASS, 1 on FAIL or build error.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
XV6_DIR="$ROOT/xv6-src"
INPUT="$ROOT/tests/inputs/smoke.in"
OUT_DIR="$ROOT/out"
RUN_LOG="$OUT_DIR/autotest.log"
FAIL_LOG="$OUT_DIR/last-fail.log"
BUILD_LOG="$OUT_DIR/build.log"
TIMEOUT="${AUTOTEST_TIMEOUT:-45}"

mkdir -p "$OUT_DIR"

if [[ ! -f "$INPUT" ]]; then
  echo "FAIL: missing smoke input at $INPUT" | tee "$FAIL_LOG"
  exit 1
fi

# Zombie sweep — a previous aborted run may leave qemu holding the serial.
pkill -f qemu-system-riscv64 >/dev/null 2>&1 || true

# Build (incremental by default; full clean only when requested).
(
  cd "$XV6_DIR"
  if [[ "${AUTOTEST_CLEAN:-0}" == "1" ]]; then
    make clean >/dev/null 2>&1 || true
  fi
  make >"$BUILD_LOG" 2>&1
) || {
  echo "FAIL: build error — see $BUILD_LOG"
  cp "$BUILD_LOG" "$FAIL_LOG"
  tail -20 "$FAIL_LOG"
  exit 1
}

# Fresh fs.img so /cache.bin starts empty: cachetest's disk-overlay
# assertions must not be satisfied by records left from a prior boot
# (incremental make does not rebuild fs.img when sources are unchanged).
if ! ( cd "$XV6_DIR" && rm -f fs.img && make fs.img ) >/dev/null 2>&1; then
  echo "FAIL: could not regenerate fs.img" | tee "$FAIL_LOG"
  exit 1
fi

# Drive xv6 through serial. The leading sleep gives the kernel time to
# reach the shell prompt; the trailing sleep keeps stdin open long enough
# for smoketest output to flush before the timeout closes the pipe.
(
  cd "$XV6_DIR"
  # After feeding the input we send Ctrl-A X — QEMU's "-nographic" quit
  # sequence — so the run finishes as soon as smoketest is done instead
  # of idling until the timeout fires.
  # Smoke input, then the Pattern A/B unit programs (verifiertest exercises
  # the kernel verifier + checkpoint/restore syscalls; cachetest exercises
  # the response cache exact/semantic/disk paths). Both run standalone (no
  # host proxy needed), so they fit the headless smoke gate.
  ( sleep 3; cat "$INPUT"; printf 'verifiertest\ncachetest\n'; sleep 4; printf '\001x' ) \
    | timeout "$TIMEOUT" qemu-system-riscv64 \
        -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic \
        -global virtio-mmio.force-legacy=false \
        -drive file=fs.img,if=none,format=raw,id=x0 \
        -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
    > "$RUN_LOG" 2>&1
) || true   # timeout/SIGTERM from killing qemu is expected — judge by log content.

if grep -q SMOKE_TEST_PASS "$RUN_LOG" \
   && grep -q VERIFIER_TEST_PASS "$RUN_LOG" \
   && grep -q CACHE_TEST_PASS "$RUN_LOG"; then
  echo "PASS"
  exit 0
fi

echo "FAIL: expected SMOKE_TEST_PASS + VERIFIER_TEST_PASS + CACHE_TEST_PASS"
cp "$RUN_LOG" "$FAIL_LOG"
echo "--- tail of $FAIL_LOG ---"
tail -20 "$FAIL_LOG"
exit 1
