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

def parse_oob_stats(run_dir: pathlib.Path):
    """Search for OOB stats in all files within run_dir and sum them up."""
    old_header = "OOB stats: <oob offset> <occurence>"
    new_header = "OOB stats: <addr> <oob offset> <occurence>"
    final_result = {
        "histogram": {},
        "addr_counts": {},
        "total_checks": 0,
        "managed_checks": 0
    }
    found_any = False

    try:
        # Check all files in the directory
        files = [f for f in run_dir.iterdir() if f.is_file()]
        
        for f in files:
            try:
                # Read file content (ignoring errors for binary files)
                content = f.read_text(errors="ignore")
                
                # Check for NEW header
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
                            
                            # Parse new format lines
                            # 1. In-bounds: "0 <count>"
                            if len(parts) == 2 and parts[0] == "0":
                                try:
                                    count = int(parts[1])
                                    final_result["histogram"][0] = final_result["histogram"].get(0, 0) + count
                                except ValueError: break
                            # 2. Out-of-bounds: "<addr> <offset> <count>"
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

                # Check for OLD header
                elif old_header in content:
                    found_any = True
                    lines = content.splitlines()

                    # First pass: look for single line stats
                    for line in lines:
                        if "Total N00B checks:" in line:
                            try:
                                val = int(line.split(":", 1)[1].strip())
                                final_result["total_checks"] += val
                            except ValueError: pass
                        elif "Total N00B-managed checks:" in line:
                            try:
                                val = int(line.split(":", 1)[1].strip())
                                final_result["managed_checks"] += val
                            except ValueError: pass

                    # Second pass: look for histogram block
                    found_header = False
                    for line in lines:
                        if old_header in line:
                            found_header = True
                            continue
                        
                        if found_header:
                            parts = line.split()
                            if len(parts) < 2:
                                # Stop if line doesn't look like "offset count"
                                if not line.strip(): continue
                                break
                            try:
                                offset = int(parts[0])
                                count = int(parts[1])
                                final_result["histogram"][offset] = final_result["histogram"].get(offset, 0) + count
                            except ValueError:
                                break
            except Exception:
                continue
    except Exception:
        pass

    if found_any:
        return final_result
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

def collect_data(code_size_glob=None, oob_stats=False):
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

        exe_path = None
        chosen_run = None
        
        if code_size_glob:
            matches = [d for d in run_subdirs if fnmatch.fnmatch(d.name, f"*{code_size_glob}*")]
            if not matches:
                results[bench] = None
                continue
            bench_short = bench.split(".", 1)[1]  # "perlbench_s"
            exe_path, chosen_run = newest_executable_among_runs(matches, bench_short)
        else:
            latest_run = max(run_subdirs, key=lambda d: d.stat().st_mtime)
            chosen_run = latest_run

        if not chosen_run:
            results[bench] = None
            continue

        value = None
        if oob_stats:
            value = parse_oob_stats(chosen_run)
        elif code_size_glob:
            value = parse_text_size(exe_path) if exe_path else None
        else:
            time_log = chosen_run / "time.log"
            value = parse_rss(time_log)

        results[bench] = {
            "value": value,
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
    ap.add_argument(
        "--oob-stats",
        action="store_true",
        help="Report OOB stats from stderr instead of RSS."
    )
    args = ap.parse_args()

    results = collect_data(code_size_glob=args.code_size, oob_stats=args.oob_stats)
    
    if args.oob_stats:
        print(f"{'Benchmark':<20} {'RunDir':<45} {'OOB Stats'}")
        for b in BENCHMARKS:
            r = results.get(b)
            if not r or r["value"] is None:
                print(f"{b:<20} {'':<45} No stats found")
            else:
                print(f"{b:<20} {r['rundir']:<45}")
                data = r["value"]
                
                # Print global counters if present
                if data.get("total_checks", 0) > 0:
                    print(f"    Total Checks:   {data['total_checks']}")
                if data.get("managed_checks", 0) > 0:
                    print(f"    Managed Checks: {data['managed_checks']}")
                
                stats = data.get("histogram", {})
                addr_counts = data.get("addr_counts", {})
                if stats:
                    # Print 0 (in-bounds) first
                    if 0 in stats:
                        print(f"    In-bounds (0): {stats[0]}")
                    # Print others sorted
                    for offset in sorted(stats.keys()):
                        if offset != 0:
                            msg = f"    Offset {offset:<10}: {stats[offset]}"
                            if offset in addr_counts:
                                msg += f" ({len(addr_counts[offset])})"
                            print(msg)
                print("")
    else:
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

