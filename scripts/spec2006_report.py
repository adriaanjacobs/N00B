#!/usr/bin/env python3
import sys
import argparse
import subprocess
import os
import csv
from pathlib import Path
from datetime import datetime

SCRIPT_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path.cwd()

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
        pass
    return None

def parse_oob_stats(run_dir: Path):
    new_header = "OOB stats: <addr> <oob offset> <occurence>"
    final_result = {
        "histogram": {},
        "addr_counts": {},
    }
    found_any = False
    try:
        files = [f for f in run_dir.iterdir() if f.is_file()]
        for f in files:
            try:
                content = f.read_text(errors="ignore")
                if new_header in content:
                    found_any = True
                    lines = content.splitlines()
                    found_header = False
                    for line in lines:
                        if new_header in line:
                            found_header = True
                            continue
                        if found_header:
                            parts = line.split()
                            if not parts: continue
                            if len(parts) == 2 and parts[0] == "0":
                                try:
                                    count = int(parts[1])
                                    final_result["histogram"][0] = final_result["histogram"].get(0, 0) + count
                                except ValueError: break
                            elif len(parts) == 3:
                                try:
                                    addr = parts[0]
                                    offset = int(parts[1])
                                    count = int(parts[2])
                                    final_result["histogram"][offset] = final_result["histogram"].get(offset, 0) + count
                                    if offset not in final_result["addr_counts"]:
                                        final_result["addr_counts"][offset] = set()
                                    final_result["addr_counts"][offset].add(addr)
                                except ValueError: break
                            else:
                                break
            except Exception:
                continue
    except Exception:
        pass
    return final_result if found_any else None

def parse_text_size(exe_path: Path):
    try:
        out = subprocess.check_output(["size", "-B", str(exe_path)], text=True)
        lines = out.strip().splitlines()
        if len(lines) < 2: return None
        headers = lines[0].split()
        values = lines[1].split()
        text_idx = headers.index("text")
        return int(values[text_idx]) / 1024.0
    except Exception:
        return None

def newest_executable_among_runs(run_dirs, bench_short, code_size_glob):
    newest_exe = None
    newest_time = -1.0
    newest_dir = None
    for run_dir in run_dirs:
        try:
            for f in run_dir.iterdir():
                if f.is_file() and os.access(f, os.X_OK) and f.name != "speccmds.cmd" and bench_short in f.name and f.name.endswith(code_size_glob):
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
                if bench in BENCH_ORDER and len(parts) >= 3:
                    nums = [p for p in parts[1:] if p.replace('.', '', 1).isdigit() and p.count('.') <= 1]
                    if len(nums) >= 2:
                        times[bench] = nums[1]
                    elif len(nums) == 1:
                        times[bench] = nums[0]
            if times:
                parts = rsf_file.name.split('.')
                run_num = parts[1] if len(parts) >= 2 else rsf_file.name
                
                if run_num not in grouped_runs:
                    grouped_runs[run_num] = {
                        "files": [],
                        "mtime": 0,
                        "times": {}
                    }
                
                grouped_runs[run_num]["files"].append(txt_file)
                grouped_runs[run_num]["mtime"] = max(grouped_runs[run_num]["mtime"], txt_file.stat().st_mtime)
                grouped_runs[run_num]["times"].update(times)
        except Exception:
            pass
            
    runs = list(grouped_runs.values())
    runs.sort(key=lambda x: x["mtime"], reverse=True)
    return runs

def select_run(config_name):
    runs = get_historical_runs(config_name)
    if not runs:
        print(f"No historical SPEC result/*.txt runs found matching config '{config_name}'.")
        return {}
        
    print(f"\nFound {len(runs)} historical SPEC run(s) for config '{config_name}':")
    for i, r in enumerate(runs):
        dt = datetime.fromtimestamp(r["mtime"]).strftime('%Y-%m-%d %H:%M:%S')
        filenames = ", ".join([f.name for f in r["files"]])
        print(f"  [{i+1}] {filenames}  -- {dt} -- ({len(r['times'])} benchmarks reported)")
        
    if len(runs) == 1:
        print("Auto-selecting the only run.")
        return runs[0]

    while True:
        try:
            choice = input(f"\nSelect a run to report on [1-{len(runs)}] (default 1): ").strip()
            if not choice:
                idx = 0
            else:
                idx = int(choice) - 1
            if 0 <= idx < len(runs):
                selected = runs[idx]
                if idx > 0:
                    print("\n" + "!"*80)
                    print("WARNING: You selected an older run!")
                    print("The Base Run Time will accurately reflect this older run.")
                    print("However, Peak RSS and Code Size are read from the newest `run_...` directory,")
                    print("which may have been overwritten by subsequent runs if older directories")
                    print("were not preserved by runspec.")
                    print("!"*80 + "\n")
                return selected
            else:
                print("Invalid choice.")
        except ValueError:
            print("Please enter a number.")
        except KeyboardInterrupt:
            sys.exit(1)

def collect_data(config_name, oob_stats=False):
    selected_run = select_run(config_name)
    run_times = selected_run.get("times", {}) if selected_run else {}
    
    results = {}
    for bench in BENCH_ORDER:
        bench_dir = BENCHSPEC_DIR / bench
        if not bench_dir.is_dir():
            results[bench] = {"rt": "", "rss": "", "text": "", "oob": None, "rundir": ""}
            continue

        run_root = bench_dir / "run"
        if not run_root.is_dir():
            results[bench] = {"rt": "", "rss": "", "text": "", "oob": None, "rundir": ""}
            continue

        run_subdirs = [d for d in run_root.iterdir() if d.is_dir() and d.name.startswith("run_")]
        matches = [d for d in run_subdirs if config_name in d.name]
        
        bench_short = SPECIAL_SHORTHANDS.get(bench, bench.split(".", 1)[1])
        exe_path, chosen_run = newest_executable_among_runs(matches, bench_short, config_name)
        
        rss = ""
        text_size = ""
        oob = None
        rundir_name = ""
        
        if chosen_run:
            rundir_name = chosen_run.name
            if oob_stats:
                oob = parse_oob_stats(chosen_run)
            else:
                rss_val = parse_rss(chosen_run / "time.log")
                if rss_val is not None:
                    rss = round(rss_val / 1024.0, 2)
            
            if exe_path:
                text_val = parse_text_size(exe_path)
                if text_val is not None:
                    text_size = round(text_val, 2)
        
        # Only populate values if this benchmark actually has a runtime from the chosen log
        is_in_run = bench in run_times
        if not is_in_run:
            results[bench] = {"rt": "", "rss": "", "text": "", "oob": None, "rundir": ""}
        else:
            results[bench] = {
                "rt": run_times[bench],
                "rss": rss,
                "text": text_size,
                "oob": oob,
                "rundir": rundir_name
            }
    return results

def check_csv_conflict(file_path, new_col_names, row_keys, key_to_row_name, data_getter):
    if not file_path.exists():
        return False
    with open(file_path, 'r', newline='', encoding='utf-8') as f:
        reader = csv.reader(f)
        rows = list(reader)
    if not rows:
        return False
    
    header = rows[0]
    indices = []
    for name in new_col_names:
        indices.append(header.index(name) if name in header else None)
            
    if all(idx is None for idx in indices):
        return False
        
    row_map = {row[0]: row for row in rows[1:] if row}
    for key in row_keys:
        row_name = key_to_row_name(key)
        new_data = data_getter(key)
        if row_name in row_map:
            row = row_map[row_name]
            for i, idx in enumerate(indices):
                if idx is not None and idx < len(row):
                    existing_val = str(row[idx]).strip()
                    new_val = str(new_data[i]).strip()
                    if existing_val and new_val and existing_val != new_val:
                        return True
    return False

def update_csv(file_path, header_prefix, row_keys, key_to_row_name, new_col_names, data_getter):
    rows = []
    if file_path.exists():
        with open(file_path, 'r', newline='', encoding='utf-8') as f:
            reader = csv.reader(f)
            rows = list(reader)

    if not rows:
        rows.append(header_prefix + new_col_names)
        for key in row_keys:
            rows.append([key_to_row_name(key)] + data_getter(key))
    else:
        header = rows[0]
        for name in new_col_names:
            if name not in header:
                header.append(name)
        
        row_map = {row[0]: row for row in rows[1:] if row}
        
        for key in row_keys:
            row_name = key_to_row_name(key)
            if row_name not in row_map:
                new_row = [row_name] + [''] * (len(header) - 1)
                insert_idx = len(rows)
                for i, r in enumerate(rows):
                    if r and r[0] == "SPECspeed 2017":
                        insert_idx = i
                        break
                rows.insert(insert_idx, new_row)
                row_map[row_name] = new_row
            
            row = row_map[row_name]
            while len(row) < len(header):
                row.append('')
            
            new_data = data_getter(key)
            for i, name in enumerate(new_col_names):
                if str(new_data[i]).strip() != "":
                    idx = header.index(name)
                    row[idx] = str(new_data[i])
                
        for r in rows:
            while len(r) < len(header):
                r.append('')

    with open(file_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerows(rows)

def print_table(results, config_name, oob_stats=False):
    print("\n" + "="*80)
    print(f"REPORT FOR CONFIG: {config_name}")
    print("="*80)
    
    if oob_stats:
        print(f"{'Benchmark':<20} {'RunDir':<45} {'OOB Stats'}")
        print("-" * 80)
        for b in BENCH_ORDER:
            r = results.get(b)
            if not r or r.get("rt") == "":
                print(f"{b:<20} {'':<45} No run recorded")
            else:
                print(f"{b:<20} {r['rundir']:<45}")
                data = r.get("oob")
                if data:
                    stats = data.get("histogram", {})
                    addr_counts = data.get("addr_counts", {})
                    if 0 in stats:
                        print(f"    In-bounds (0): {stats[0]}")
                    for offset in sorted(stats.keys()):
                        if offset != 0:
                            msg = f"    Offset {offset:<10}: {stats[offset]}"
                            if offset in addr_counts:
                                msg += f" ({len(addr_counts[offset])})"
                            print(msg)
                else:
                    print("    No OOB stats found.")
                print("")
    else:
        print(f"{'Benchmark':<20} | {'Base Run Time':<15} | {'Peak RSS (MB)':<15} | {'.text Size (KB)':<15}")
        print("-" * 75)
        for b in BENCH_ORDER:
            r = results.get(b)
            if not r or r.get("rt") == "":
                print(f"{b:<20} | {'':<15} | {'':<15} | {'':<15}")
            else:
                rt = r.get("rt", "")
                rss = r.get("rss", "")
                txt = r.get("text", "")
                print(f"{b:<20} | {str(rt):<15} | {str(rss):<15} | {str(txt):<15}")
    print("="*80 + "\n")

def main():
    ap = argparse.ArgumentParser(description="SPEC CPU2006 runtime, RSS and .text reporter")
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
        help="Update CSV files in the current working directory. Optionally specify a prefix (e.g. --csv=aurora)."
    )
    ap.add_argument(
        "--oob-stats",
        action="store_true",
        help="Report OOB stats from stderr instead of RSS/Runtime."
    )
    args = ap.parse_args()

    results = collect_data(args.config_name, oob_stats=args.oob_stats)
    
    print_table(results, args.config_name, oob_stats=args.oob_stats)

    if args.csv is not None:
        prefix = args.csv
        rt_name = f"{prefix}-spec-rt.csv" if prefix else "spec-rt.csv"
        mem_name = f"{prefix}-spec-rss-text.csv" if prefix else "spec-rss-text.csv"
        
        runtime_csv = OUTPUT_DIR / rt_name
        mem_csv = OUTPUT_DIR / mem_name
        
        print(f"Updating CSVs: {rt_name} & {mem_name}")

        rt_col_names = [args.config_name]
        mem_col_names = [f"{args.config_name} peak RSS (MB)", f"{args.config_name} .text size (KB)"]

        if check_csv_conflict(runtime_csv, rt_col_names, BENCH_ORDER, lambda k: k, lambda k: [results[k]["rt"]]):
            ans = input(f"WARNING: {rt_name} already has data for '{args.config_name}' that would be overwritten. Proceed? [y/N]: ")
            if ans.lower() != 'y':
                print(f"Skipping update for {rt_name}.")
                return
                
        if check_csv_conflict(mem_csv, mem_col_names, BENCH_ORDER, lambda k: k, lambda k: [results[k]["rss"], results[k]["text"]]):
            ans = input(f"WARNING: {mem_name} already has data for '{args.config_name}' that would be overwritten. Proceed? [y/N]: ")
            if ans.lower() != 'y':
                print(f"Skipping update for {mem_name}.")
                return

        update_csv(
            file_path=runtime_csv,
            header_prefix=["SPEC CPU 2006"],
            row_keys=BENCH_ORDER,
            key_to_row_name=lambda k: k,
            new_col_names=rt_col_names,
            data_getter=lambda k: [results[k]["rt"]]
        )

        update_csv(
            file_path=mem_csv,
            header_prefix=["SPEC CPU 2006"],
            row_keys=BENCH_ORDER,
            key_to_row_name=lambda k: k,
            new_col_names=mem_col_names,
            data_getter=lambda k: [results[k]["rss"], results[k]["text"]]
        )

        print(f"Successfully updated CSV files!")

if __name__ == "__main__":
    main()
