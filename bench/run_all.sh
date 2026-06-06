#!/usr/bin/env bash
# bench/run_all.sh — Liberal_OS evaluation harness.
#
# Runs:
#   1. E2E mock pipeline (parallel xv6 procs)   — N iterations.
#   2. E2E mock pipeline (sequential baseline)  — N iterations. Same 5
#      stages but chained via shell ';' + file redirection; each stage
#      completes before the next starts. The wall-clock ratio against
#      run #1 is the empirical baseline for the speedup claim — see
#      bench/report.py §4.
#   3. Priority scheduling probe (priotest)     — 1 iteration.
#   4. Evaluator retry effectiveness            — derived from #1's JSONs.
#
# Env knobs:
#   BENCH_N=<n>          — iterations per topology (default 5)
#   BENCH_TIMEOUT=<sec>  — per-run timeout (default 60)
#
# Outputs:
#   out/bench/e2e/e2e_*.json   raw e2e JSON per parallel iteration
#   out/bench/seq/seq_*.json   raw e2e JSON per sequential iteration
#   out/bench/e2e/summary.json aggregated stats over e2e_*.json
#   out/bench/seq/summary.json aggregated stats over seq_*.json
#   out/bench/priotest.json    single priotest JSON
#   out/REPORT.md              composed markdown report
# Safe to re-run; raw files are overwritten.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT="$ROOT/out/bench"
E2E_DIR="$OUT/e2e"
SEQ_DIR="$OUT/seq"
N="${BENCH_N:-5}"
TIMEOUT="${BENCH_TIMEOUT:-60}"

mkdir -p "$E2E_DIR" "$SEQ_DIR"
# clean stale artifacts (legacy parent + per-topology dirs) so summarize
# doesn't pick up files from a previous N=larger run.
rm -f "$OUT"/e2e_*.json "$OUT"/summary.json 2>/dev/null || true
rm -f "$E2E_DIR"/*.json "$SEQ_DIR"/*.json 2>/dev/null || true

# Zombie sweep — any leftover qemu from an aborted run holds the serial.
pkill -f qemu-system-riscv64 >/dev/null 2>&1 || true

run_one() {
  # $1 = topology label, $2 = output JSON path, $3+ = proxy_daemon flags
  local label="$1" out_file="$2"; shift 2
  # Retry once on timeout — qemu occasionally drops the first boot.
  local attempt
  for attempt in 1 2; do
    pkill -f qemu-system-riscv64 >/dev/null 2>&1 || true
    sleep 0.3
    (
      cd "$ROOT"
      python3 host/proxy_daemon.py "$@"
    ) > "$out_file"
    local ok
    ok=$(python3 -c "import json; d=json.load(open('$out_file')); print(d.get('ok'))")
    if [[ "$ok" == "True" ]]; then return 0; fi
    [[ "$attempt" == 2 ]] && return 1
    echo "  [$label] attempt $attempt failed, retrying"
  done
}

echo "[bench 1/3] e2e mock parallel — $N iterations, ${TIMEOUT}s each"
for i in $(seq 1 "$N"); do
  out_file="$E2E_DIR/e2e_$i.json"
  run_one "parallel" "$out_file" --mode mock --triage short.log --timeout "$TIMEOUT" || true
  ok=$(python3 -c "import json; d=json.load(open('$out_file')); print(d.get('ok'))")
  el=$(python3 -c "import json; d=json.load(open('$out_file')); print(d.get('elapsed_s'))")
  echo "  e2e run $i: ok=$ok elapsed_s=$el"
done

echo "[bench 2/3] e2e mock sequential — $N iterations, ${TIMEOUT}s each"
for i in $(seq 1 "$N"); do
  out_file="$SEQ_DIR/seq_$i.json"
  run_one "sequential" "$out_file" --mode mock --triage-sequential short.log --timeout "$TIMEOUT" || true
  ok=$(python3 -c "import json; d=json.load(open('$out_file')); print(d.get('ok'))")
  el=$(python3 -c "import json; d=json.load(open('$out_file')); print(d.get('elapsed_s'))")
  echo "  seq run $i: ok=$ok elapsed_s=$el"
done

echo "[bench 3/3] priotest (priority scheduling probe)"
prio_file="$OUT/priotest.json"
(
  cd "$ROOT"
  python3 host/proxy_daemon.py --priotest --timeout "$TIMEOUT"
) > "$prio_file"
ok=$(python3 -c "import json; d=json.load(open('$prio_file')); print(d.get('ok'))")
rho=$(python3 -c "import json; d=json.load(open('$prio_file')); print(d.get('rho_prio_vs_order'))")
echo "  priotest: ok=$ok rho_prio_vs_order=$rho"

echo "[bench] summarize e2e (parallel)"
python3 "$ROOT/bench/summarize.py" "$E2E_DIR" > "$E2E_DIR/summary.json"
cat "$E2E_DIR/summary.json"
echo

echo "[bench] summarize e2e (sequential)"
python3 "$ROOT/bench/summarize.py" "$SEQ_DIR" > "$SEQ_DIR/summary.json"
cat "$SEQ_DIR/summary.json"
echo

echo
echo "[bench] generate REPORT.md"
python3 "$ROOT/bench/report.py" "$E2E_DIR/summary.json" \
  --seq-summary "$SEQ_DIR/summary.json" \
  --priotest "$prio_file" --bench-dir "$E2E_DIR" > "$ROOT/out/REPORT.md"
echo "[bench] wrote $ROOT/out/REPORT.md"
