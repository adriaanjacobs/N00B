#!/usr/bin/env python3
import argparse
import pathlib
import subprocess
import os
import fnmatch

ROOT = pathlib.Path(".").resolve()

# Benchmarks of interest (SPEC2017 speed, excluding Fortran)
BENCHMARKS = [
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

def parse_rss(time_log: pathlib.Path):
    if not time_log.exists():
        return None
    try:
        with open(time_log) as f:
            for line in f:
                if "Maximum resident set size" in line:
                    return float(line.split(":", 1)[1].strip())
    except Exception:
        pass
    return None

def parse_text_size(exe_path: pathlib.Path):
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

def newest_executable_among_runs(run_dirs, bench_short):
    """
    Match executables containing the benchmark short name WITHOUT trailing '_s'.
    """
    short_no_s = bench_short[:-2] if bench_short.endswith("_s") else bench_short
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
                    and short_no_s in f.name
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
    for bench in BENCHMARKS:
        bench_dir = ROOT / "benchspec/CPU" / bench
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
            matches = [d for d in run_subdirs if fnmatch.fnmatch(d.name, f"*{code_size_glob}*")]
            if not matches:
                results[bench] = None
                continue
            bench_short = bench.split(".", 1)[1]  # "perlbench_s"
            exe_path, chosen_run = newest_executable_among_runs(matches, bench_short)
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
    ap = argparse.ArgumentParser(description="SPEC CPU2017 RSS or .text code size reporter")
    ap.add_argument(
        "--code-size",
        metavar="GLOB",
        help="Report .text size (KB, two decimals) instead of RSS. "
             "Argument is a glob/substring to select run dirs (e.g., clang15-lto-baseline)."
    )
    args = ap.parse_args()

    results = collect_data(code_size_glob=args.code_size)
    colname = "CodeSize(KB)" if args.code_size else "RSS(KB)"
    print(f"{'Benchmark':<20} {'RunDir':<45} {colname}")
    for b in BENCHMARKS:
        r = results.get(b)
        if not r or r["value"] is None:
            print(f"{b:<20} {'':<45} ")
        else:
            print(f"{b:<20} {r['rundir']:<45} {r['value']:.2f}")

if __name__ == "__main__":
    main()

