#! /usr/bin/python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys


def plot_results_from_csv(csv_filename):
    # Read the CSV file
    df = pd.read_csv(csv_filename)
    
    # Convert comma decimal separators to periods and convert to float
    for col in ['baseline', 'N00B', 'N00Balloc']:
        df[col] = pd.to_numeric(df[col].str.replace(',', '.'), errors='coerce')

    # Calculate ratios
    df['virtual_baseline'] = df[['baseline', 'N00Balloc']].min(axis=1)
    df['N00B_ratio'] = df['N00B'] / df['virtual_baseline']
    df['N00Balloc_ratio'] = df['N00Balloc'] / df['baseline']

    # Split into separate dataframes - look for _s suffix to identify SPEC2017 benchmarks
    df_2006 = df[~df.iloc[:, 0].str.contains('_s', na=False)].copy()  # No _s suffix = SPEC2006
    df_2017 = df[df.iloc[:, 0].str.contains('_s', na=False)].copy()  # With _s suffix = SPEC2017
    
    # Remove the "SPECspeed 2017" header row from df_2017
    df_2017 = df_2017[df_2017.iloc[:, 0].str.contains('\d', na=False)]  # Keep only rows with numbers

    # Print dataframes to verify splitting
    print("\nSPEC CPU2006 data:")
    print(df_2006)
    print("\nSPECspeed 2017 data:")
    print(df_2017)

if __name__ == '__main__':
    # Check if CSV filenames are provided
    if len(sys.argv) < 2:
        print("Usage: ./plot_results.py <csv_files...>")
        sys.exit(1)

    # Process each CSV file
    for csv_filename in sys.argv[1:]:
        print(f"Processing {csv_filename} ...")
        plot_results_from_csv(csv_filename)

