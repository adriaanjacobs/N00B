import sys
import subprocess
import os
import csv
from pathlib import Path
from datetime import datetime

def parse_rss(time_log: Path):
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

def parse_oob_stats(run_dir: Path):
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
                elif old_header in content:
                    found_any = True
                    lines = content.splitlines()
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
                    found_header = False
                    for line in lines:
                        if old_header in line:
                            found_header = True
                            continue
                        if found_header:
                            parts = line.split()
                            if len(parts) < 2:
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

def select_run(config_name, get_historical_runs_func, rss_count=0, text_count=0):
    runs = get_historical_runs_func(config_name)
    if not runs:
        print(f"No historical SPEC result/*.txt runs found matching config '{config_name}'.")
        return {}
        
    print(f"\nFound {len(runs)} historical SPEC run(s) for config '{config_name}':")
    for i, r in enumerate(runs):
        dt = datetime.fromtimestamp(r["mtime"]).strftime('%Y-%m-%d %H:%M:%S')
        filenames = ", ".join([f.name for f in r["files"]])
        
        sizes = set()
        for f in r["files"]:
            parts = f.name.split('.')
            if len(parts) >= 3:
                sizes.add(parts[-2].upper())
        size_str = "/".join(sorted(list(sizes))) if sizes else "UNKNOWN"
        
        rt_count = sum(1 for val in r['times'].values() if val != "")
        print(f"  [{i+1}] [{size_str} RUN] {filenames}  -- {dt} -- ({rt_count} RT, {rss_count} Peak RSS, {text_count} .text reported)")
        
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
                    print("were not preserved.")
                    print("!"*80 + "\n")
                return selected
            else:
                print("Invalid choice.")
        except ValueError:
            print("Please enter a number.")
        except KeyboardInterrupt:
            sys.exit(1)

def check_csv_conflict(file_path, new_col_names, row_keys, data_getter):
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
        new_data = data_getter(key)
        if key in row_map:
            row = row_map[key]
            for i, idx in enumerate(indices):
                if idx is not None and idx < len(row):
                    existing_val = str(row[idx]).strip()
                    new_val = str(new_data[i]).strip()
                    if existing_val and new_val and existing_val != new_val:
                        return True
    return False

def update_csv(file_path, header_prefix, row_keys, new_col_names, data_getter, spec_group="2006"):
    rows = []
    if file_path.exists():
        with open(file_path, 'r', newline='', encoding='utf-8') as f:
            reader = csv.reader(f)
            rows = list(reader)

    if not rows:
        rows.append(header_prefix + new_col_names)
        
        if spec_group == "2017":
            # Add SPECspeed 2017 separator for new files WITH REPEATED HEADERS
            spec_header = ["SPECspeed 2017"] + new_col_names
            rows.append(spec_header)
            
        for key in row_keys:
            rows.append([key] + data_getter(key))
    else:
        header = rows[0]
        for name in new_col_names:
            if name not in header:
                header.append(name)
        
        row_map = {row[0]: row for row in rows[1:] if row}
        
        for key in row_keys:
            if key not in row_map:
                new_row = [key] + [''] * (len(header) - 1)
                
                if spec_group == "2017":
                    # Check if we need to insert SPECspeed 2017 header if it's missing.
                    if "SPECspeed 2017" not in row_map:
                        spec_header = ["SPECspeed 2017"] + header[1:]
                        rows.append(spec_header)
                        row_map["SPECspeed 2017"] = spec_header
                    
                    rows.append(new_row)
                    row_map[key] = new_row
                else:
                    # For 2006, insert BEFORE SPECspeed 2017 if it exists
                    insert_idx = len(rows)
                    for i, r in enumerate(rows):
                        if r and r[0] == "SPECspeed 2017":
                            insert_idx = i
                            break
                    rows.insert(insert_idx, new_row)
                    row_map[key] = new_row
            
            row = row_map[key]
            while len(row) < len(header):
                row.append('')
            
            new_data = data_getter(key)
            for i, name in enumerate(new_col_names):
                if str(new_data[i]).strip() != "":
                    idx = header.index(name)
                    row[idx] = str(new_data[i])
                    
        # Ensure SPECspeed 2017 header stays up to date with new columns
        if "SPECspeed 2017" in row_map:
            spec_row = row_map["SPECspeed 2017"]
            while len(spec_row) < len(header):
                spec_row.append('')
            for i, name in enumerate(new_col_names):
                idx = header.index(name)
                spec_row[idx] = name
                
        for r in rows:
            while len(r) < len(header):
                r.append('')

    with open(file_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerows(rows)

def print_table(results, config_name, bench_order, oob_stats=False):
    print("\n" + "="*80)
    print(f"REPORT FOR CONFIG: {config_name}")
    print("="*80)
    
    if oob_stats:
        print(f"{'Benchmark':<20} {'RunDir':<45} {'OOB Stats'}")
        print("-" * 80)
        for b in bench_order:
            r = results.get(b)
            if not r or r.get("rt") == "":
                print(f"{b:<20} {'':<45} No run recorded")
            else:
                print(f"{b:<20} {r['rundir']:<45}")
                data = r.get("oob")
                if data:
                    if data.get("total_checks", 0) > 0:
                        print(f"    Total Checks:   {data['total_checks']}")
                    if data.get("managed_checks", 0) > 0:
                        print(f"    Managed Checks: {data['managed_checks']}")
                    
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
        for b in bench_order:
            r = results.get(b)
            if not r:
                print(f"{b:<20} | {'':<15} | {'':<15} | {'':<15}")
            else:
                rt = r.get("rt", "")
                rss = r.get("rss", "")
                txt = r.get("text", "")
                print(f"{b:<20} | {str(rt):<15} | {str(rss):<15} | {str(txt):<15}")
    print("="*80 + "\n")
