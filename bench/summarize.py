"""Summarize bench JSON files into a single-line JSON.

Reads every *.json file under the given directory (recursively), groups numeric
fields by their key, and emits one summary line per key with mean / stdev / n.
This lets Claude Code read the summary instead of all 25 raw bench files
(see HARNESS.md §4.8 — context-saver).

Usage:
    python bench/summarize.py out/bench/
    python bench/summarize.py out/bench/ --keys speedup,isolation_rate
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
from typing import Any


def _collect(path: pathlib.Path) -> dict[str, list[float]]:
    grouped: dict[str, list[float]] = {}
    for jf in sorted(path.rglob("*.json")):
        try:
            data = json.loads(jf.read_text())
        except json.JSONDecodeError as exc:
            print(f"warn: skip {jf} ({exc})", file=sys.stderr)
            continue
        if not isinstance(data, dict):
            continue
        for k, v in data.items():
            if isinstance(v, (int, float)) and not isinstance(v, bool):
                grouped.setdefault(k, []).append(float(v))
    return grouped


def _stats(samples: list[float]) -> dict[str, float]:
    n = len(samples)
    if n == 0:
        return {"mean": 0.0, "stdev": 0.0, "n": 0}
    mean = sum(samples) / n
    if n == 1:
        stdev = 0.0
    else:
        var = sum((s - mean) ** 2 for s in samples) / (n - 1)
        stdev = math.sqrt(var)
    return {"mean": round(mean, 4), "stdev": round(stdev, 4), "n": n}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dir", type=pathlib.Path)
    parser.add_argument("--keys", default="", help="comma-separated subset of keys")
    args = parser.parse_args()

    if not args.dir.exists():
        print(json.dumps({"ok": False, "error": f"{args.dir} not found"}))
        return 1

    grouped = _collect(args.dir)
    if args.keys:
        wanted = {k.strip() for k in args.keys.split(",") if k.strip()}
        grouped = {k: v for k, v in grouped.items() if k in wanted}

    summary: dict[str, Any] = {k: _stats(v) for k, v in sorted(grouped.items())}
    print(json.dumps({"ok": True, "summary": summary}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
