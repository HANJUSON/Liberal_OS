#!/usr/bin/env bash
# tests/test_verifier.sh — Pattern A closed-loop gate (T-83).
#
# Drives the triage pipeline in MOCK mode through the host proxy daemon and
# confirms the kernel-guarded verify+rollback loop fires end to end:
#   - at least one candidate fix proposal is rejected by the kernel verifier,
#     rolled back, and retried  (EVAL_RETRY -> daemon's eval_retries > 0), and
#   - the corrected proposal is ultimately accepted (evaluator:OK ->
#     eval_oks > 0), with the run reported ok.
#
# This reproduces the VERIFY FAIL -> ROLLBACK -> RETRY -> ACCEPT sequence.
# Mock-only, deterministic, no network. Exits 0 on PASS, 1 on FAIL.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$ROOT/out"
JSON="$OUT_DIR/test_verifier.json"
FAIL_LOG="$OUT_DIR/last-fail.log"
mkdir -p "$OUT_DIR"

# Zombie sweep so a previous aborted run doesn't hold the serial.
pkill -f qemu-system-riscv64 >/dev/null 2>&1 || true

cd "$ROOT"
python3 host/proxy_daemon.py --mode mock --triage short.log > "$JSON" 2>/dev/null

if python3 - "$JSON" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception as e:
    print(f"bad json: {e}"); sys.exit(1)
retries = d.get("eval_retries", 0)
oks = d.get("eval_oks", 0)
ok = bool(d.get("ok", False))
print(f"ok={ok} eval_retries={retries} eval_oks={oks}")
sys.exit(0 if (ok and retries > 0 and oks > 0) else 1)
PY
then
  echo "PASS"
  exit 0
fi

echo "FAIL: verify+rollback loop not observed (need ok && eval_retries>0 && eval_oks>0)"
cp "$JSON" "$FAIL_LOG" 2>/dev/null || true
cat "$JSON" 2>/dev/null
exit 1
