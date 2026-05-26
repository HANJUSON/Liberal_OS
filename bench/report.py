"""bench/report.py — render bench/summarize.py output into REPORT.md.

Inputs:
    out/bench/summary.json   (see bench/summarize.py)
Output:
    stdout — Markdown report with one row per numeric metric.

Phase 4/5 experiments (evaluator retry quality, scheduler priority)
are intentionally absent until T-41 / T-52 land; the report calls them
out as TODO so reviewers can tell what is measured vs deferred.
"""
from __future__ import annotations

import argparse
import datetime
import json
import pathlib
import sys


def render(summary_path: pathlib.Path) -> str:
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
    lines.append("## E2E mock pipeline (parallel xv6 procs)")
    lines.append("")
    lines.append("| Metric | Mean | Stdev | N |")
    lines.append("|---|---:|---:|---:|")
    for key in sorted(summary):
        stats = summary[key]
        lines.append(f"| {key} | {stats['mean']} | {stats['stdev']} | {stats['n']} |")
    lines.append("")
    lines.append("Each row aggregates one numeric field across the captured runs of")
    lines.append("`python3 host/proxy_daemon.py --mode mock --triage short.log`.")
    lines.append("")
    lines.append("## Deferred experiments")
    lines.append("")
    lines.append("These rely on phases that have not landed yet:")
    lines.append("")
    lines.append("- **Evaluator retry quality (T-41)** — needs the `kill` + `sleep/wakeup`")
    lines.append("  signal loop in Evaluator before before/after quality is meaningful.")
    lines.append("- **Priority scheduling effect (T-52)** — needs the priority queue")
    lines.append("  rewrite of `scheduler()` (HUMAN GATE in MASTER_PLAN.md).")
    lines.append("- **Sequential vs parallel speedup** — needs a sequential baseline")
    lines.append("  variant of `triage` running all five stages in one proc per line.")
    lines.append("")
    lines.append("These are tracked in MASTER_PLAN.md and will appear here once the")
    lines.append("underlying tasks ship.")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("summary", type=pathlib.Path)
    args = ap.parse_args()
    if not args.summary.exists():
        print(f"# REPORT\n\nMissing summary: {args.summary}\n")
        return 1
    sys.stdout.write(render(args.summary))
    return 0


if __name__ == "__main__":
    sys.exit(main())
