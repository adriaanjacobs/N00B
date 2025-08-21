#!/usr/bin/env python3
import re
from pathlib import Path

SPEC_ROOT = Path.cwd()
BENCHSPEC_DIR = SPEC_ROOT / "benchspec/CPU2006"

# Order of benchmarks (CINT2006 first, then CFP2006)
BENCH_ORDER = [
    "400.perlbench",
    "401.bzip2",
    "403.gcc",
    "429.mcf",
    "445.gobmk",
    "456.hmmer",
    "458.sjeng",
    "462.libquantum",
    "464.h264ref",
    "471.omnetpp",
    "473.astar",
    "483.xalancbmk",
    "433.milc",
    "444.namd",
    "447.dealII",
    "450.soplex",
    "453.povray",
    "470.lbm",
    "482.sphinx3",
]

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

    # Pick the most recent time.log file
    time_logs = list(latest_run.glob("time.log*"))
    if not time_logs:
        continue
    time_log = max(time_logs, key=lambda p: p.stat().st_mtime)

    # Extract benchmark name from path
    benchmark_name = time_log.parts[6]

    # Parse RSS
    with open(time_log) as f:
        content = f.read()
    match = rss_re.search(content)
    if match:
        rss_kb = int(match.group(1))
        rss_data[benchmark_name] = rss_kb / 1024.0  # MB

# Print results in SPEC order
print(f"{'Benchmark':20s} {'RSS(MB)':>10s}")
for bench in BENCH_ORDER:
    if bench in rss_data:
        print(f"{bench:<20s} {rss_data[bench]:10.1f}")
