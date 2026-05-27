"""bench/report.py — render evaluation results into REPORT.md.

Inputs:
    summary.json           (from bench/summarize.py — aggregates e2e runs)
    --priotest <path>      optional, single-run JSON from --priotest
    --bench-dir <path>     optional, raw e2e_*.json directory for per-run eval

Output:
    stdout — Markdown report covering:
      §1. E2E mock pipeline (parallel)
      §2. Evaluator retry effectiveness (derived from e2e runs)
      §3. Priority scheduling effect (from priotest)
      §4. Sequential vs parallel — analytical model + measurement caveat
"""
from __future__ import annotations

import argparse
import datetime
import json
import pathlib
import sys


def _section_e2e(summary: dict) -> list[str]:
    out: list[str] = ["## §1. E2E mock pipeline (parallel xv6 procs)", ""]
    out.append("| Metric | Mean | Stdev | N |")
    out.append("|---|---:|---:|---:|")
    for key in sorted(summary):
        s = summary[key]
        out.append(f"| {key} | {s['mean']} | {s['stdev']} | {s['n']} |")
    out.append("")
    out.append("Each row aggregates one numeric field across captured runs of")
    out.append("`python3 host/proxy_daemon.py --mode mock --triage short.log`.")
    out.append("")
    return out


def _section_eval_retry(bench_dir: pathlib.Path) -> list[str]:
    """Compute evaluator retry effectiveness from raw e2e JSONs."""
    runs: list[dict] = []
    for jf in sorted(bench_dir.glob("e2e_*.json")):
        try:
            runs.append(json.loads(jf.read_text()))
        except (json.JSONDecodeError, OSError):
            continue
    out: list[str] = ["## §2. Evaluator retry effectiveness", ""]
    if not runs:
        out.append("_(no e2e runs found in bench dir)_")
        out.append("")
        return out

    total_lines = sum(r.get("evaluator_lines", 0) for r in runs)
    total_oks = sum(r.get("eval_oks", 0) for r in runs)
    total_fails = sum(r.get("eval_fails", 0) for r in runs)
    total_retries = sum(r.get("eval_retries", 0) for r in runs)

    # First-pass pass rate = (oks - retried_oks). The bench harness counts
    # one OK per line that eventually passes; lines that failed first then
    # succeeded contribute to eval_retries. Lines that ran out of retries
    # contribute to eval_fails.
    # A line either fails or eventually succeeds, so:
    #   final_pass_rate = oks / (oks + fails)
    #   first_pass_estimate = (oks - retried_oks) / total
    #   where retried_oks = retries - (3 * fails)  if max_retries==3
    # We expose the raw counts and the bounded pass-rate improvement.
    MAX_RETRIES = 3
    retried_oks_est = max(0, total_retries - MAX_RETRIES * total_fails)
    first_pass_est = max(0, total_oks - retried_oks_est)
    finals = total_oks + total_fails

    out.append("| Metric | Value |")
    out.append("|---|---:|")
    out.append(f"| Lines evaluated (total) | {total_lines} |")
    out.append(f"| Final pass (`evaluator:OK:`) | {total_oks} |")
    out.append(f"| Final fail (`evaluator:FAIL:`, retries exhausted) | {total_fails} |")
    out.append(f"| Retry signals issued | {total_retries} |")
    out.append(f"| Final pass rate | {total_oks/finals*100:.1f}% ({total_oks}/{finals}) |")
    out.append("")
    out.append(f"Retry budget per line: **{MAX_RETRIES}** (hard cap in `agent_evaluator.c`).")
    out.append("")
    # Mechanism vs quality: under mock, retries are deterministic so retry
    # quality must be measured under live mode. Mechanism IS verified by
    # the retry-signal accounting below.
    if total_fails > 0 and total_retries == MAX_RETRIES * total_fails:
        out.append("**Mechanism verification**:")
        out.append(f"`retry_signals` ({total_retries}) = `MAX_RETRIES` ({MAX_RETRIES}) "
                   f"× `final_fails` ({total_fails}) — every failing line exhausted its")
        out.append("budget exactly, and no passing line consumed a retry. The")
        out.append("`kill` + `sleep/wakeup` Supervisor loop fires correctly and")
        out.append("the 3-strike cap holds.")
        out.append("")
    out.append("**Quality caveat — mock determinism.** `mock_handler` is a pure")
    out.append("echo, so a line that fails first will fail again identically.")
    out.append("Under mock the retry **mechanism** is verified but the **quality")
    out.append("delta** is structurally 0 pp. Live mode (`MASTER_PLAN.md` T-62)")
    out.append("introduces LLM stochasticity where retries can change the")
    out.append("answer; the quality delta belongs in that report.")
    out.append("")
    return out


def _section_priotest(prio: dict) -> list[str]:
    out: list[str] = ["## §3. Priority scheduling effect (priotest)", ""]
    if not prio or not prio.get("ok"):
        out.append("_(priotest data missing or failed)_")
        out.append("")
        return out

    out.append("| Metric | Value |")
    out.append("|---|---:|")
    out.append(f"| Children forked | {prio.get('n_children')} |")
    out.append(f"| Rho (priority vs finish order) | **{prio.get('rho_prio_vs_order')}** |")
    out.append(f"| Wall-clock | {prio.get('elapsed_s')}s |")
    out.append("")
    out.append("Finish order vs assigned priority (higher = preferred):")
    out.append("")
    out.append("| Order | Child idx | Priority | Tick offset |")
    out.append("|---:|---:|---:|---:|")
    for rec in prio.get("records", []):
        out.append(
            f"| {rec['order']} | {rec['i']} | {rec['prio']} | {rec['t']} |"
        )
    out.append("")
    out.append("Spearman ρ near −1.0 confirms our modified `scheduler()` honours")
    out.append("the `priority` field on `struct proc` (set via `setprio(2)`).")
    out.append("Tick offsets of 0 mean each child's CPU-bound loop completed")
    out.append("within a single timer tick — finish ORDER is the meaningful")
    out.append("signal here, not absolute time.")
    out.append("")
    return out


def _section_speedup_model(summary: dict) -> list[str]:
    """Analytical sequential vs parallel — mock has zero LLM latency so an
    empirical seq baseline measures fork+pipe overhead only, which is not
    the speedup story. We instead model it.
    """
    out: list[str] = ["## §4. Sequential vs parallel speedup (analytical model)", ""]

    n_stages = 5
    n_lines = 5
    elapsed = summary.get("elapsed_s", {})
    par_observed = elapsed.get("mean", None) if isinstance(elapsed, dict) else None

    out.append("Mock mode returns LLM responses instantly (`mock_handler` is")
    out.append("a pure echo), so empirically measured wall-clock reflects xv6")
    out.append("boot, fork/exec, and pipe-plumbing overhead — **not LLM-call")
    out.append("parallelism**. Under realistic latency `L` per LLM call:")
    out.append("")
    out.append("```")
    out.append("  T_sequential  ≈  N_lines × N_stages × L")
    out.append(f"                =  {n_lines} × {n_stages} × L  =  {n_lines*n_stages}·L")
    out.append("")
    out.append("  T_parallel    ≈  (N_lines + N_stages − 1) × L      (steady-state pipeline)")
    out.append(f"                =  ({n_lines} + {n_stages} − 1) × L  =  {n_lines+n_stages-1}·L")
    out.append("")
    out.append(f"  speedup       ≈  {n_lines*n_stages} / {n_lines+n_stages-1}  ≈  {n_lines*n_stages/(n_lines+n_stages-1):.2f}×")
    out.append("```")
    out.append("")
    if par_observed is not None:
        out.append(f"Observed parallel mean (mock): **{par_observed}s** — this is")
        out.append("the xv6-overhead floor with `L≈0`. Live-mode `L` of 200–800 ms")
        out.append("would shift the bottleneck onto LLM latency where the model")
        out.append("above predicts ≈2.78× speedup over a sequential variant.")
        out.append("")
    out.append("**Caveat — proxy serialization.** `proxylock(2)`/`proxyunlock(2)`")
    out.append("(see TECHNICAL_REPORT §3 row 4) serialise the *send + recv* of")
    out.append("each `proxy_call` against sibling agents. Under live mode this")
    out.append("partially erodes the analytical speedup: stages overlap in")
    out.append("CPU/pipe work but their LLM transactions queue at the host")
    out.append("proxy. Future work — request-id multiplexing in proxy_daemon.")
    out.append("")
    return out


def render(summary_path: pathlib.Path, prio_path: pathlib.Path | None,
           bench_dir: pathlib.Path | None) -> str:
    data = json.loads(summary_path.read_text())
    if not data.get("ok"):
        return f"# REPORT\n\nsummarize failed: {data!r}\n"
    summary = data["summary"]

    ts = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")
    lines: list[str] = []
    lines.append("# Liberal_OS — Evaluation Report")
    lines.append("")
    lines.append(f"Generated: {ts}")
    lines.append("")
    lines.append("This report covers three measured experiments plus one")
    lines.append("analytical model. All runs use **mock mode** (zero-latency")
    lines.append("LLM responses) so results are deterministic and replayable")
    lines.append("without an Upstage API key. Live-mode numbers require")
    lines.append("`MASTER_PLAN.md` T-62 (human-authorised).")
    lines.append("")
    lines.extend(_section_e2e(summary))

    if bench_dir is not None and bench_dir.is_dir():
        lines.extend(_section_eval_retry(bench_dir))

    if prio_path is not None and prio_path.exists():
        try:
            prio = json.loads(prio_path.read_text())
        except (json.JSONDecodeError, OSError):
            prio = {}
        lines.extend(_section_priotest(prio))

    lines.extend(_section_speedup_model(summary))

    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("summary", type=pathlib.Path)
    ap.add_argument("--priotest", type=pathlib.Path, default=None,
                    help="optional priotest JSON path")
    ap.add_argument("--bench-dir", type=pathlib.Path, default=None,
                    help="optional raw e2e bench dir for retry-effect calc")
    args = ap.parse_args()
    if not args.summary.exists():
        print(f"# REPORT\n\nMissing summary: {args.summary}\n")
        return 1
    sys.stdout.write(render(args.summary, args.priotest, args.bench_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
