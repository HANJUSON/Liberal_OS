#!/usr/bin/env bash
# tests/e2e_mock.sh — Liberal_OS T-36 end-to-end gate.
#
# Drives the full 5-agent triage pipeline in xv6 under mock mode and
# PASS/FAIL based on the JSON the proxy daemon emits. Exits 0 if every
# stage served the expected number of requests; non-zero otherwise.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$ROOT/out"
RUN_LOG="$OUT_DIR/e2e_mock.log"
FAIL_LOG="$OUT_DIR/last-fail.log"

mkdir -p "$OUT_DIR"

(
  cd "$ROOT"
  python3 host/proxy_daemon.py --mode mock --triage short.log --timeout 30
) > "$RUN_LOG" 2>&1

if python3 -c "
import json, sys
d = json.loads(open('$RUN_LOG').read().strip().splitlines()[-1])
sys.exit(0 if d.get('ok') else 1)
" ; then
  cat "$RUN_LOG"
  echo "PASS"
  exit 0
fi

echo "FAIL"
cp "$RUN_LOG" "$FAIL_LOG"
cat "$FAIL_LOG"
exit 1
