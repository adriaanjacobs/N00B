import sys
import re
import xml.etree.ElementTree as ET

def main():
    # Check if a filename is provided as an argument
    if len(sys.argv) > 1:
        try:
            with open(sys.argv[1], 'r') as f:
                input_data = f.read()
        except FileNotFoundError:
            print(f"Error: File '{sys.argv[1]}' not found.", file=sys.stderr)
            return
    else:
        # Read from stdin if no argument provided
        if sys.stdin.isatty():
             print("Paste your XML content below and press Ctrl+D (Linux/Mac) or Ctrl+Z (Windows):", file=sys.stderr)
        input_data = sys.stdin.read()
    
    if not input_data.strip():
        return

    try:
        root = ET.fromstring(input_data)
    except ET.ParseError:
        print("Error: Input does not appear to be valid XML.", file=sys.stderr)
        return

    # Dictionary to hold grouped results
    # Key: Result Identifier (e.g., local/ffmpeg-baseline)
    # Value: List of tuples (encoder, scenario, fps)
    grouped_results = {}
    
    # Regex for description parsing
    # Expected: "Encoder: libx264 - Scenario: Live"
    desc_re = re.compile(r"Encoder:\s+(.+?)\s+-\s+Scenario:\s+(.+)")

    for result in root.findall('Result'):
        identifier_node = result.find('Identifier')
        description_node = result.find('Description')
        
        if identifier_node is None or description_node is None:
            continue

        identifier = identifier_node.text
        description = description_node.text
        
        # Extract Value from Data -> Entry -> Value
        # Uses the first entry found (assuming single system context from composite.xml)
        data_node = result.find('Data')
        if data_node is None: continue
            
        entry_node = data_node.find('Entry')
        if entry_node is None: continue
            
        value_node = entry_node.find('Value')
        if value_node is None: continue
            
        fps_raw = value_node.text
        
        # Format FPS to 2 decimal places for cleaner spreadsheet pasting
        try:
            fps_str = f"{float(fps_raw):.2f}"
        except (ValueError, TypeError):
            fps_str = fps_raw

        # Parse description for Encoder/Scenario
        match = desc_re.search(description)
        if match:
            encoder = match.group(1).strip()
            scenario = match.group(2).strip()
            
            if identifier not in grouped_results:
                grouped_results[identifier] = []
            
            grouped_results[identifier].append((encoder, scenario, fps_str))

    # Output Loop
    for identifier, rows in grouped_results.items():
        print(f"{identifier}:")

        # Sort rows: libx264 first, then libx265, then others. Secondary sort by scenario.
        def sort_key(row):
            encoder, scenario, _ = row
            if encoder == 'libx264':
                prio = 0
            elif encoder == 'libx265':
                prio = 1
            else:
                prio = 2
            return (prio, encoder, scenario)

        rows.sort(key=sort_key)

        if not rows:
            print()
            continue

        # Calculate column widths for alignment
        max_enc_len = max(len(r[0]) for r in rows)
        max_scen_len = max(len(r[1]) for r in rows)
        
        # Add spacing (e.g., 4 spaces)
        enc_col_width = max_enc_len + 1
        scen_col_width = max_scen_len + 4

        # Printing header row is usually safer for Sheets to auto-detect columns
        # print("Encoder\tScenario\tFPS") 
        for row in rows:
            encoder, scenario, fps = row
            print(f"{encoder:<{enc_col_width}}{scenario:<{scen_col_width}}{fps}")
        print() # Separator line

if __name__ == "__main__":
    main()
