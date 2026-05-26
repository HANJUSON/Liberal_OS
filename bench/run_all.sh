#!/usr/bin/env bash
# bench/run_all.sh — Liberal_OS evaluation harness.
#
# Runs the e2e mock pipeline N times (default 5) and captures one JSON
# per run plus a summary. Phase 4/5-dependent experiments (Evaluator
# retry quality, scheduler priority effect) are intentionally skipped
# until those phases land; see MASTER_PLAN.md T-41 / T-52.
#
# Env knobs:
#   BENCH_N=<n>          — number of e2e iterations (default 5)
#   BENCH_TIMEOUT=<sec>  — per-run timeout (default 30)
#
# Outputs all artifacts under out/bench/. Safe to re-run.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT="$ROOT/out/bench"
N="${BENCH_N:-5}"
TIMEOUT="${BENCH_TIMEOUT:-30}"

mkdir -p "$OUT"

echo "[bench] e2e mock — $N iterations, ${TIMEOUT}s each"
for i in $(seq 1 "$N"); do
  out_file="$OUT/e2e_$i.json"
  (
    cd "$ROOT"
    python3 host/proxy_daemon.py --mode mock --triage short.log --timeout "$TIMEOUT"
  ) > "$out_file"
  ok=$(python3 -c "import json; d=json.load(open('$out_file')); print(d.get('ok'))")
  el=$(python3 -c "import json; d=json.load(open('$out_file')); print(d.get('elapsed_s'))")
  echo "  run $i: ok=$ok elapsed_s=$el"
done

echo "[bench] summarize"
python3 "$ROOT/bench/summarize.py" "$OUT" > "$OUT/summary.json"
cat "$OUT/summary.json"

echo
echo "[bench] generate REPORT.md"
python3 "$ROOT/bench/report.py" "$OUT/summary.json" > "$ROOT/out/REPORT.md"
echo "[bench] wrote $ROOT/out/REPORT.md"
