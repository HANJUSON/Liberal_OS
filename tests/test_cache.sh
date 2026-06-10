#!/usr/bin/env bash
# tests/test_cache.sh — Pattern B short-circuit gate (T-88).
#
# Runs the triage pipeline in MOCK mode through the host proxy daemon and
# confirms the kernel response cache skips LLM calls (PROXY_REQ emissions)
# on repeated prompts.
#
# The evaluator (T-83) re-issues the *same* (role, prompt) proxy_call on a
# verify-driven retry. Without the cache each retry would emit its own
# PROXY_REQ, so the evaluator's served count would be parser-count +
# eval_retries. With the cache short-circuit (T-88) the retry hits the
# cache and emits no PROXY_REQ, so the evaluator emits no more PROXY_REQ
# than the parser despite doing extra proxy_call()s — that difference is
# the count of LLM calls the cache saved.
#
# fs.img is regenerated first so /cache.bin starts empty (deterministic).
# Mock-only, no network. Exits 0 on PASS, 1 on FAIL.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$ROOT/out"
JSON="$OUT_DIR/test_cache.json"
FAIL_LOG="$OUT_DIR/last-fail.log"
mkdir -p "$OUT_DIR"

pkill -f qemu-system-riscv64 >/dev/null 2>&1 || true

# Fresh filesystem image => empty /cache.bin => deterministic cache state.
if ! ( cd "$ROOT/xv6-src" && rm -f fs.img && make fs.img ) >/dev/null 2>&1; then
  echo "FAIL: could not regenerate fs.img (a stale warm cache would flake this gate)"
  exit 1
fi

cd "$ROOT"
python3 host/proxy_daemon.py --mode mock --triage short.log > "$JSON" 2>/dev/null

if python3 - "$JSON" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception as e:
    print(f"bad json: {e}"); sys.exit(1)
served = d.get("served", {})
ev = served.get("evaluator", 0)
pa = served.get("parser", 0)
retries = d.get("eval_retries", 0)
ok = bool(d.get("ok", False))
saved = (pa + retries) - ev      # PROXY_REQs the cache short-circuited
print(f"ok={ok} served_parser={pa} served_evaluator={ev} "
      f"eval_retries={retries} proxy_reqs_saved={saved}")
sys.exit(0 if (ok and retries > 0 and ev <= pa and saved > 0) else 1)
PY
then
  echo "PASS"
  exit 0
fi

echo "FAIL: cache did not skip PROXY_REQ on repeated prompts"
cp "$JSON" "$FAIL_LOG" 2>/dev/null || true
cat "$JSON" 2>/dev/null
exit 1
