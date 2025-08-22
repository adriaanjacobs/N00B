#!/usr/bin/env python3
import pathlib
import re

ROOT = pathlib.Path(".")
TIMELOG_PATTERN = re.compile(r"Maximum resident set size.*: (\d+)")

BENCH_ORDER = [
    "600.perlbench_s",
    "602.gcc_s",
    "605.mcf_s",
    "620.omnetpp_s",
    "623.xalancbmk_s",
    "625.x264_s",
    "631.deepsjeng_s",
    "641.leela_s",
    "657.xz_s",
    "619.lbm_s",
    "638.imagick_s",
    "644.nab_s",
]

def parse_rss(file_path):
    with open(file_path, "r") as f:
        for line in f:
            m = TIMELOG_PATTERN.search(line)
            if m:
                return int(m.group(1)) / 1024.0  # KB → MB
    return None

def collect_data():
    results = {}
    for logfile in ROOT.glob("benchspec/CPU/*/run/*/time.log"):
        parts = logfile.parts
        benchmark = parts[-4]  # e.g. "600.perlbench_s"
        rss = parse_rss(logfile)
        if rss is not None:
            results[benchmark] = rss
    return results

def print_table(results):
    print(f"{'Benchmark':20s} {'RSS(MB)':>10s}")
    for bench in BENCH_ORDER:
        rss = results.get(bench)
        if rss is not None:
            print(f"{bench:<20s} {rss:10.1f}")
        else:
            print(f"{bench:<20s} {'':>10s}")

if __name__ == "__main__":
    data = collect_data()
    print_table(data)

