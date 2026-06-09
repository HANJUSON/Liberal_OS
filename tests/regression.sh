#!/usr/bin/env bash
# tests/regression.sh — pre-commit regression gate.
#
# Five stages, all must pass:
#   1. autotest.sh        (xv6 boot + smoketest + verifiertest + cachetest)
#   2. xv6 shell smoke    (basic ls/echo over serial)
#   3. proxy hello (mock) (host/hello_upstage.py emits ok:true)
#   4. test_verifier.sh   (Pattern A verify+rollback closed loop, mock)
#   5. test_cache.sh      (Pattern B cache short-circuit, mock)
#
# Exits 0 on PASS, 1 on the first failing stage.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
XV6_DIR="$ROOT/xv6-src"
OUT_DIR="$ROOT/out"
RUN_LOG="$OUT_DIR/regression.log"
SHELL_LOG="$OUT_DIR/regression-shell.log"
FAIL_LOG="$OUT_DIR/last-fail.log"

mkdir -p "$OUT_DIR"
: > "$RUN_LOG"

stage() { printf '==> %s\n' "$*" | tee -a "$RUN_LOG"; }

stage "1/5 autotest"
if ! bash "$SCRIPT_DIR/autotest.sh" >>"$RUN_LOG" 2>&1; then
  echo "FAIL: autotest"
  cp "$RUN_LOG" "$FAIL_LOG"
  tail -25 "$FAIL_LOG"
  exit 1
fi

stage "2/5 xv6 shell (ls + echo)"
pkill -f qemu-system-riscv64 >/dev/null 2>&1 || true
(
  cd "$XV6_DIR"
  ( sleep 3; printf 'ls\necho REGRESSION_SHELL_OK\n'; sleep 3; printf '\001x' ) \
    | timeout 30 qemu-system-riscv64 \
        -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic \
        -global virtio-mmio.force-legacy=false \
        -drive file=fs.img,if=none,format=raw,id=x0 \
        -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
    > "$SHELL_LOG" 2>&1
) || true
if ! grep -q 'README' "$SHELL_LOG" || ! grep -q 'REGRESSION_SHELL_OK' "$SHELL_LOG"; then
  echo "FAIL: xv6 shell did not echo expected tokens"
  cp "$SHELL_LOG" "$FAIL_LOG"
  tail -25 "$FAIL_LOG"
  exit 1
fi

stage "3/5 proxy hello (mock)"
PYOUT="$(MODE=mock python3 "$ROOT/host/hello_upstage.py" 2>&1)" || true
printf '%s\n' "$PYOUT" >> "$RUN_LOG"
if ! printf '%s' "$PYOUT" | python3 -c 'import json,sys; d=json.loads(sys.stdin.read()); sys.exit(0 if d.get("ok") else 1)'; then
  echo "FAIL: proxy hello did not return ok:true"
  printf '%s\n' "$PYOUT" > "$FAIL_LOG"
  tail -25 "$FAIL_LOG"
  exit 1
fi

stage "4/5 verify+rollback loop (test_verifier.sh)"
if ! bash "$SCRIPT_DIR/test_verifier.sh" >>"$RUN_LOG" 2>&1; then
  echo "FAIL: test_verifier"
  cp "$RUN_LOG" "$FAIL_LOG"
  tail -25 "$FAIL_LOG"
  exit 1
fi

stage "5/5 cache short-circuit (test_cache.sh)"
if ! bash "$SCRIPT_DIR/test_cache.sh" >>"$RUN_LOG" 2>&1; then
  echo "FAIL: test_cache"
  cp "$RUN_LOG" "$FAIL_LOG"
  tail -25 "$FAIL_LOG"
  exit 1
fi

echo "PASS"
exit 0
