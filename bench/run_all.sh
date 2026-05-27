#!/usr/bin/env bash
# bench/run_all.sh — Liberal_OS evaluation harness.
#
# Runs:
#   1. E2E mock pipeline (parallel xv6 procs) — N iterations.
#   2. Priority scheduling probe (priotest)   — 1 iteration (deterministic).
#   3. Evaluator retry effectiveness          — derived from #1's JSONs.
#
# Env knobs:
#   BENCH_N=<n>          — e2e iterations (default 5)
#   BENCH_TIMEOUT=<sec>  — per-run timeout (default 30)
#
# Outputs:
#   out/bench/e2e/e2e_*.json   raw e2e JSON per iteration
#   out/bench/e2e/summary.json aggregated numeric stats over e2e_*.json
#   out/bench/priotest.json    single priotest JSON
#   out/REPORT.md              composed markdown report
# Safe to re-run; e2e files are overwritten.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT="$ROOT/out/bench"
E2E_DIR="$OUT/e2e"
N="${BENCH_N:-5}"
TIMEOUT="${BENCH_TIMEOUT:-30}"

mkdir -p "$E2E_DIR"
# clean stale legacy artifacts to keep summarize honest
rm -f "$OUT"/e2e_*.json "$OUT"/summary.json 2>/dev/null || true

echo "[bench 1/2] e2e mock — $N iterations, ${TIMEOUT}s each"
for i in $(seq 1 "$N"); do
  out_file="$E2E_DIR/e2e_$i.json"
  (
    cd "$ROOT"
    python3 host/proxy_daemon.py --mode mock --triage short.log --timeout "$TIMEOUT"
  ) > "$out_file"
  ok=$(python3 -c "import json; d=json.load(open('$out_file')); print(d.get('ok'))")
  el=$(python3 -c "import json; d=json.load(open('$out_file')); print(d.get('elapsed_s'))")
  echo "  e2e run $i: ok=$ok elapsed_s=$el"
done

echo "[bench 2/2] priotest (priority scheduling probe)"
prio_file="$OUT/priotest.json"
(
  cd "$ROOT"
  python3 host/proxy_daemon.py --priotest --timeout "$TIMEOUT"
) > "$prio_file"
ok=$(python3 -c "import json; d=json.load(open('$prio_file')); print(d.get('ok'))")
rho=$(python3 -c "import json; d=json.load(open('$prio_file')); print(d.get('rho_prio_vs_order'))")
echo "  priotest: ok=$ok rho_prio_vs_order=$rho"

echo "[bench] summarize e2e"
python3 "$ROOT/bench/summarize.py" "$E2E_DIR" > "$E2E_DIR/summary.json"
cat "$E2E_DIR/summary.json"

echo
echo "[bench] generate REPORT.md"
python3 "$ROOT/bench/report.py" "$E2E_DIR/summary.json" \
  --priotest "$prio_file" --bench-dir "$E2E_DIR" > "$ROOT/out/REPORT.md"
echo "[bench] wrote $ROOT/out/REPORT.md"
