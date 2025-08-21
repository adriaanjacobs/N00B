#!/usr/bin/env python3
import re
from pathlib import Path

SPEC_ROOT = Path.cwd()
BENCHSPEC_DIR = SPEC_ROOT / "benchspec/CPU2006"

rss_re = re.compile(r"Maximum resident set size.*:\s+(\d+)")

rss_data = {}

for bench_dir in BENCHSPEC_DIR.iterdir():
    if not bench_dir.is_dir():
        continue
    run_dir = bench_dir / "run"
    if not run_dir.is_dir():
        continue

    # Pick the latest run_* directory by modification time
    run_subdirs = [d for d in run_dir.iterdir() if d.is_dir() and d.name.startswith("run_")]
    if not run_subdirs:
        continue
    latest_run = max(run_subdirs, key=lambda p: p.stat().st_mtime)

    # Pick the time.log file
    time_logs = list(latest_run.glob("time.log*"))
    if not time_logs:
        continue
    time_log = max(time_logs, key=lambda p: p.stat().st_mtime)

    # Extract benchmark name from path
    # e.g., benchspec/CPU2006/400.perlbench/run/...
    benchmark_name = time_log.parts[6]

    # Parse RSS
    with open(time_log) as f:
        content = f.read()
    match = rss_re.search(content)
    if match:
        rss_kb = int(match.group(1))
        rss_data[benchmark_name] = rss_kb / 1024.0  # MB

# Print results
print(f"{'Benchmark':20s} {'RSS(MB)':>10s}")
for bench in sorted(rss_data):
    print(f"{bench:<20s} {rss_data[bench]:10.1f}")

