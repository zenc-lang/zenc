#!/usr/bin/env python3
"""Magnitude-aware benchmark regression check.

The Benchmarks workflow stores every run's results in the gh-pages branch
(dev/bench/data.js) through benchmark-action/github-action-benchmark. That
action can only apply one global alert threshold, but on GitHub-hosted runners
a micro-benchmark (e.g. 1ms) routinely doubles or triples from pure noise
while a longer measurement is far more stable. Additionally, comparing against
the single previous run makes the result jumpy.

This script therefore compares the current run against the median of the last
few runs, using thresholds that scale with the benchmark magnitude:

    baseline < 5ms    -> 4.0x  (noise floor; tiny values are dominated by noise)
    5ms .. 20ms       -> 3.0x
    20ms .. 100ms     -> 2.0x
    >= 100ms          -> 1.5x

A sustained regression will eventually move the median past the threshold; a
single noisy run will not.

Exit code 0 when no regression is detected, 1 otherwise.
"""

import json
import os
import re
import subprocess
import sys

DATA_PATH = os.environ.get("BENCH_DATA_PATH", "dev/bench/data.js")
GH_PAGES = os.environ.get("GH_PAGES_REF", "origin/gh-pages")
BASELINE_RUNS = int(os.environ.get("BENCH_BASELINE_RUNS", "5"))


def magnitude_threshold(baseline):
    if baseline < 5:
        return 4.0
    if baseline < 20:
        return 3.0
    if baseline < 100:
        return 2.0
    return 1.5


def median(values):
    ordered = sorted(values)
    n = len(ordered)
    if n % 2 == 1:
        return ordered[n // 2]
    return (ordered[n // 2 - 1] + ordered[n // 2]) / 2.0


def previous_entries():
    subprocess.run(
        ["git", "fetch", "origin", "gh-pages"],
        check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    raw = subprocess.run(
        ["git", "show", "%s:%s" % (GH_PAGES, DATA_PATH)],
        capture_output=True, text=True, check=False,
    )
    if raw.returncode != 0:
        return []
    match = re.search(r"window\.BENCHMARK_DATA\s*=\s*(\{.*\})", raw.stdout, re.DOTALL)
    if not match:
        return []
    try:
        data = json.loads(match.group(1))
    except ValueError:
        return []
    return [entry for suite in data.get("entries", {}).values() for entry in suite]


def main():
    try:
        with open("benchmarks_result.json") as f:
            current = {b["name"]: b["value"] for b in json.load(f)}
    except (OSError, ValueError):
        print("-> benchmarks_result.json not found; skipping check.")
        return 0

    entries = previous_entries()
    # The last stored entry is this run (the action just pushed it); the
    # baseline is the median of the runs before it.
    history = entries[:-1][-BASELINE_RUNS:]
    if not history:
        print("-> Not enough stored benchmark data yet; skipping comparison.")
        return 0

    by_name = {}
    for entry in history:
        for bench in entry.get("benches", []):
            by_name.setdefault(bench["name"], []).append(bench["value"])

    regressions = []
    for name, cur in current.items():
        baseline = median(by_name[name]) if name in by_name else None
        if baseline is None or baseline <= 0 or cur <= 0:
            continue
        ratio = cur / baseline
        threshold = magnitude_threshold(baseline)
        if ratio > threshold:
            regressions.append((name, baseline, cur, ratio, threshold))

    if regressions:
        print("-> Benchmark regression detected (magnitude-aware, baseline = median of %d prior runs):"
              % len(history))
        for name, baseline, cur, ratio, threshold in regressions:
            print("   %s: %.3g -> %.3g (%.2fx, threshold %.1fx)"
                  % (name, baseline, cur, ratio, threshold))
        return 1

    print("-> No benchmark regression detected (magnitude-aware, baseline = median of %d prior runs)."
          % len(history))
    return 0


if __name__ == "__main__":
    sys.exit(main())
