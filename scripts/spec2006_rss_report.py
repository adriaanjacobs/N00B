#!/usr/bin/env python3
import argparse
import subprocess
import fnmatch
import os
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

# Special-case shorthand mapping
SPECIAL_SHORTHANDS = {
    "483.xalancbmk": "Xalan",
    "482.sphinx3": "sphinx",
}

def parse_rss(time_log: Path):
    try:
        with open(time_log) as f:
            for line in f:
                if "Maximum resident set size" in line:
                    return float(line.split(":", 1)[1].strip())
    except Exception:
        return None
    return None

def parse_text_size(exe_path: Path):
    try:
        out = subprocess.check_output(["size", "-B", str(exe_path)], text=True)
        lines = out.strip().splitlines()
        if len(lines) < 2:
            return None
        headers = lines[0].split()
        values = lines[1].split()
        text_idx = headers.index("text")
        size_bytes = int(values[text_idx])
        return size_bytes / 1024.0
    except Exception:
        return None

def newest_executable_among_runs(run_dirs, bench_short, code_size_glob):
    """Find newest executable in run dirs containing bench_short and ending with code_size_glob."""
    newest_exe = None
    newest_time = -1.0
    newest_dir = None
    for run_dir in run_dirs:
        try:
            for f in run_dir.iterdir():
                if (
                    f.is_file()
                    and os.access(f, os.X_OK)
                    and f.name != "speccmds.cmd"
                    and bench_short in f.name
                    and f.name.endswith(code_size_glob)
                ):
                    mtime = f.stat().st_mtime
                    if mtime > newest_time:
                        newest_exe = f
                        newest_time = mtime
                        newest_dir = run_dir
        except Exception:
            continue
    return newest_exe, newest_dir

def collect_data(code_size_glob=None):
    results = {}
    for bench in BENCH_ORDER:
        bench_dir = BENCHSPEC_DIR / bench
        if not bench_dir.is_dir():
            results[bench] = None
            continue

        run_root = bench_dir / "run"
        if not run_root.is_dir():
            results[bench] = None
            continue

        run_subdirs = [d for d in run_root.iterdir() if d.is_dir() and d.name.startswith("run_")]
        if not run_subdirs:
            results[bench] = None
            continue

        if code_size_glob:
            matches = [d for d in run_subdirs if code_size_glob in d.name]
            if not matches:
                results[bench] = None
                continue

            # Pick shorthand (special case or normal)
            bench_short = SPECIAL_SHORTHANDS.get(bench, bench.split(".", 1)[1])
            exe_path, chosen_run = newest_executable_among_runs(matches, bench_short, code_size_glob)
            if exe_path is None:
                results[bench] = None
                continue
            value_kb = parse_text_size(exe_path)
        else:
            latest_run = max(run_subdirs, key=lambda d: d.stat().st_mtime)
            time_log = latest_run / "time.log"
            value_kb = parse_rss(time_log)
            chosen_run = latest_run

        results[bench] = {
            "value": value_kb,
            "rundir": chosen_run.name if chosen_run else "",
        }
    return results

def main():
    ap = argparse.ArgumentParser(description="SPEC CPU2006 RSS or .text code size reporter")
    ap.add_argument(
        "--code-size",
        metavar="SUFFIX",
        help="Report .text size (KB, two decimals) instead of RSS. "
             "Argument must match the suffix of executables/run dirs "
             "(e.g., .clang15-lto-baseline)."
    )
    args = ap.parse_args()

    results = collect_data(code_size_glob=args.code_size)
    colname = "CodeSize(KB)" if args.code_size else "RSS(KB)"
    print(f"{'Benchmark':<20} {'RunDir':<45} {colname}")
    for b in BENCH_ORDER:
        r = results.get(b)
        if not r or r["value"] is None:
            print(f"{b:<20} {'':<45} ")
        else:
            print(f"{b:<20} {r['rundir']:<45} {r['value']:.2f}")

if __name__ == "__main__":
    main()

