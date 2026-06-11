import pandas as pd
import numpy as np
import argparse
import os

def geomean(series):
    """Computes the geometric mean, ignoring NaN and non-positive values."""
    series = series.dropna()
    series = series[series > 0]
    if series.empty:
        return np.nan
    return np.exp(np.mean(np.log(series)))

def generate_latex_table(csv_filename):
    """
    Loads data from the specified CSV and generates a LaTeX table
    with RSS and .text section overheads.
    """
    # --- 1. Load Data ---
    try:
        df = pd.read_csv(csv_filename)
        benchmark_col = df.columns[0]
        
        # Auto-detect columns based on substrings
        rss_base_col = next((c for c in df.columns if 'baseline' in c.lower() and 'rss' in c.lower()), None)
        rss_n00b_col = next((c for c in df.columns if ('n00b' in c.lower() or 'noob' in c.lower()) and 'rss' in c.lower()), None)
        text_base_col = next((c for c in df.columns if 'baseline' in c.lower() and 'text' in c.lower()), None)
        text_n00b_col = next((c for c in df.columns if ('n00b' in c.lower() or 'noob' in c.lower()) and 'text' in c.lower()), None)

        if not all([rss_base_col, rss_n00b_col, text_base_col, text_n00b_col]):
            print(f"FATAL: Could not auto-detect necessary columns in {csv_filename}")
            print(f"Found columns: {df.columns.tolist()}")
            return
            
        df = df.rename(columns={
            benchmark_col: 'benchmark',
            rss_base_col: 'rss_base',
            rss_n00b_col: 'rss_n00b',
            text_base_col: 'text_base',
            text_n00b_col: 'text_n00b'
        })
        
        df = df[['benchmark', 'rss_base', 'rss_n00b', 'text_base', 'text_n00b']]
        
    except Exception as e:
        print(f"FATAL: Failed to load CSV. Error: {e}")
        return

    # --- 2. Clean Data ---
    # Filter out any intermediate header rows.
    df = df[~df['benchmark'].str.contains("SPEC", na=False)].reset_index(drop=True)

    # **THE DEFINITIVE FIX IS HERE:**
    # For each numeric column, we must first remove the quotes from the string,
    # and THEN convert it to a number, telling pandas to use a comma decimal.
    for col in ['rss_base', 'rss_n00b', 'text_base', 'text_n00b']:
        # Ensure the column is treated as a string before trying to replace characters.
        df[col] = df[col].astype(str).str.replace('"', '')
        # Now convert to numeric, handling the comma.
        df[col] = pd.to_numeric(df[col].str.replace(',', '.'), errors='coerce')

    # Drop any rows that had missing values (like for 450.soplex).
    df.dropna(inplace=True)

    # --- 3. Calculate Ratios and Geomeans ---
    df['rss_ratio'] = df['rss_n00b'] / df['rss_base']
    df['text_ratio'] = df['text_n00b'] / df['text_base']
    df['suite'] = df['benchmark'].apply(lambda x: '2017' if '_s' in x else '2006')

    suites = ['2006', '2017']
    geomeans = {}
    for suite in suites:
        sub_df = df[df['suite'] == suite]
        geomeans[suite] = {
            'rss_ratio': geomean(sub_df['rss_ratio']),
            'text_ratio': geomean(sub_df['text_ratio'])
        }

    # --- 4. Generate LaTeX Table Rows ---
    def format_suite(suite_name):
        suite_df = df[df['suite'] == suite_name]
        if suite_df.empty:
            return ""
        
        geomean_labels = {
            '2006': 'SPEC CPU2006',
            '2017': 'SPECspeed 2017'
        }

        rows = []
        for _, row in suite_df.iterrows():
            benchmark_name = str(row['benchmark']).replace('_', r'\_')
            # Create the row string, preserving decimal precision and ending with \\.
            row_str = (
                f"{benchmark_name} & "
                f"{row['rss_base']:.1f} & {row['rss_ratio']:.2f}× & "
                f"{row['text_base']:.2f} & {row['text_ratio']:.2f}×\\\\"
            )
            rows.append(row_str)
        
        gm = geomeans[suite_name]
        # Add the geomean row with the new custom label.
        geomean_row = (
            f"\\textbf{{{geomean_labels.get(suite_name, 'Geomean')}}} & & "
            f"\\textbf{{{gm['rss_ratio']:.2f}×}} & & "
            f"\\textbf{{{gm['text_ratio']:.2f}×}}\\\\"
        )
        rows.append(geomean_row)
        
        # Join all rows with a simple newline character.
        return "\n".join(rows)

    # --- 5. Assemble Final LaTeX Table ---
    # Updated template with new headers and table environment.
    latex_template = r"""\begin{{table}}[t]
    \centering
    \scriptsize
    \caption{{Peak Resident Set Size (RSS) and \texttt{{.text}} section size overhead for SPEC C/C++ benchmarks. Baseline columns show absolute sizes, while \NOOB columns show the ratio of overhead compared to the baseline.}}
    \label{{tab:memoverhead}}
    \begin{{tabular}}{{l rrrr}}
        \toprule
        & \multicolumn{{2}}{{c}}{{\textbf{{Peak RSS}}}} & \multicolumn{{2}}{{c}}{{\textbf{{.text Size}}}} \\
        \cmidrule(lr){{2-3}} \cmidrule(lr){{4-5}}
        Benchmark & Baseline (MB) & \NOOB & Baseline (KB) & \NOOB \\
        \midrule
        {suite_2006}
        \midrule
        {suite_2017}
        \bottomrule
    \end{{tabular}}
\end{{table}}""" + "\n"

    latex_string = latex_template.format(
        suite_2006=format_suite('2006'),
        suite_2017=format_suite('2017')
    )

    # --- 6. Write to File ---
    output_filename = "memory_and_code_size_table.tex"
    with open(output_filename, "w") as f:
        f.write(latex_string)
    print(f"Successfully generated LaTeX table at: {output_filename}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate a LaTeX table for RSS and code size from a CSV file.")
    parser.add_argument("csv_file", help="Path to the input CSV file.")
    args = parser.parse_args()
    
    generate_latex_table(args.csv_file)
