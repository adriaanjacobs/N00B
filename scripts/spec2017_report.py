#!/usr/bin/env python3
import argparse
import os
import fnmatch
from pathlib import Path
from spec_report_utils import parse_rss, parse_oob_stats, parse_text_size, select_run, check_csv_conflict, update_csv, print_table

SCRIPT_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path.cwd()

SPEC_ROOT = Path.cwd()
BENCHSPEC_DIR = SPEC_ROOT / "benchspec/CPU"

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

def newest_executable_among_runs(run_dirs, bench_short):
    short_no_s = bench_short[:-2] if bench_short.endswith("_s") else bench_short
    newest_exe = None
    newest_time = -1.0
    newest_dir = None
    for run_dir in run_dirs:
        try:
            for f in run_dir.iterdir():
                if f.is_file() and os.access(f, os.X_OK) and f.name != "speccmds.cmd" and short_no_s in f.name:
                    mtime = f.stat().st_mtime
                    if mtime > newest_time:
                        newest_exe = f
                        newest_time = mtime
                        newest_dir = run_dir
        except Exception:
            continue
    return newest_exe, newest_dir

def get_historical_runs(config_name):
    result_dir = SPEC_ROOT / "result"
    if not result_dir.is_dir():
        return []
    
    grouped_runs = {}
    for rsf_file in result_dir.glob("*.rsf"):
        try:
            content = rsf_file.read_text(errors="ignore")
            if config_name not in content:
                continue
                
            txt_file = rsf_file.with_suffix(".txt")
            if not txt_file.exists():
                continue
                
            txt_content = txt_file.read_text(errors="ignore")
            times = {}
            for line in txt_content.splitlines():
                parts = line.strip().split()
                if not parts: continue
                bench = parts[0]
                if bench in BENCHMARKS and len(parts) >= 3:
                    if 'RE' in parts or 'X' in parts or 'CE' in parts:
                        if bench not in times:
                            times[bench] = ""
                        continue
                    nums = [p for p in parts[1:] if p.replace('.', '', 1).isdigit() and p.count('.') <= 1]
                    if len(nums) >= 3:
                        times[bench] = nums[-2]
            if times:
                parts = rsf_file.name.split('.')
                run_num = parts[1] if len(parts) >= 2 else rsf_file.name
                
                if run_num not in grouped_runs:
                    grouped_runs[run_num] = {"files": [], "mtime": 0, "times": {}}
                
                grouped_runs[run_num]["files"].append(txt_file)
                grouped_runs[run_num]["mtime"] = max(grouped_runs[run_num]["mtime"], txt_file.stat().st_mtime)
                grouped_runs[run_num]["times"].update(times)
        except Exception:
            pass
            
    runs = list(grouped_runs.values())
    runs.sort(key=lambda x: x["mtime"], reverse=True)
    return runs

def collect_data(config_name, oob_stats=False):
    disk_metrics = {}
    rss_count = 0
    text_count = 0
    
    for bench in BENCHMARKS:
        bench_dir = BENCHSPEC_DIR / bench
        disk_metrics[bench] = {"rss": "", "text": "", "oob": None, "rundir": ""}
        
        if not bench_dir.is_dir(): continue
        run_root = bench_dir / "run"
        if not run_root.is_dir(): continue

        run_subdirs = [d for d in run_root.iterdir() if d.is_dir() and d.name.startswith("run_")]
        matches = [d for d in run_subdirs if fnmatch.fnmatch(d.name, f"*{config_name}*")]
        
        bench_short = bench.split(".", 1)[1]
        exe_path, chosen_run = newest_executable_among_runs(matches, bench_short)
        
        if chosen_run:
            rundir_name = chosen_run.name
            oob = None
            rss = ""
            text_size = ""
            
            if oob_stats:
                oob = parse_oob_stats(chosen_run)
            else:
                rss_val = parse_rss(chosen_run / "time.log")
                if rss_val is not None:
                    rss = round(rss_val / 1024.0, 2)
                    rss_count += 1
            
            if exe_path:
                text_val = parse_text_size(exe_path)
                if text_val is not None:
                    text_size = round(text_val, 2)
                    text_count += 1
                    
            disk_metrics[bench] = {"rss": rss, "text": text_size, "oob": oob, "rundir": rundir_name}

    chosen_run = select_run(config_name, get_historical_runs, rss_count=rss_count, text_count=text_count)
    run_times = chosen_run.get("times", {}) if chosen_run else {}

    results = {}
    for bench in BENCHMARKS:
        is_in_run = bench in run_times
        if not is_in_run:
            results[bench] = {"rt": "", "rss": "", "text": "", "oob": None, "rundir": ""}
        else:
            dm = disk_metrics[bench]
            results[bench] = {
                "rt": run_times[bench],
                "rss": dm["rss"],
                "text": dm["text"],
                "oob": dm["oob"],
                "rundir": dm["rundir"]
            }
    return results

def main():
    ap = argparse.ArgumentParser(description="SPEC CPU2017 runtime, RSS and .text reporter")
    ap.add_argument(
        "config_name",
        help="The name of the configuration (e.g. clang15-lto-CHANGE). "
             "This must match the executable suffix and run directory name."
    )
    ap.add_argument(
        "--csv",
        nargs='?',
        const="",
        default=None,
        help="Update CSV file in the current working directory. Optionally specify a full filename (e.g. --csv=results.csv). Defaults to spec-results.csv if passed without value."
    )
    ap.add_argument(
        "--name",
        type=str,
        default=None,
        help="Custom name to use for display and CSV column headers. Defaults to config_name."
    )
    ap.add_argument(
        "--oob-stats",
        action="store_true",
        help="Report OOB stats from stderr instead of RSS/Runtime."
    )
    args = ap.parse_args()

    results = collect_data(args.config_name, oob_stats=args.oob_stats)
    
    display_name = args.name if args.name else args.config_name
    
    print_table(results, display_name, BENCHMARKS, oob_stats=args.oob_stats)

    if args.csv is not None:
        csv_name = args.csv if args.csv else "spec-results.csv"
        csv_file = OUTPUT_DIR / csv_name
        
        print(f"Updating CSV: {csv_name}")

        col_names = [display_name, f"{display_name} peak RSS (MB)", f"{display_name} .text size (KB)"]

        if check_csv_conflict(csv_file, col_names, BENCHMARKS, lambda k: [results[k]["rt"], results[k]["rss"], results[k]["text"]]):
            ans = input(f"WARNING: {csv_name} already has data for '{display_name}' that would be overwritten. Proceed? [y/N]: ")
            if ans.lower() != 'y':
                print(f"Skipping update for {csv_name}.")
                return

        update_csv(
            file_path=csv_file,
            header_prefix=["SPEC CPU 2006"], 
            row_keys=BENCHMARKS,
            new_col_names=col_names,
            data_getter=lambda k: [results[k]["rt"], results[k]["rss"], results[k]["text"]],
            spec_group="2017"
        )

        print(f"Successfully updated CSV file!")

if __name__ == "__main__":
    main()
