#! /usr/bin/python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys


def plot_results_from_csv(csv_filename):
    # Read the CSV file, forcing string type for numeric columns
    df = pd.read_csv(csv_filename, dtype={'baseline': str, 'N00B': str, 'N00Balloc': str})

    # Convert comma decimal separators to periods and convert to float
    for col in ['baseline', 'N00B', 'N00Balloc']:
        df[col] = df[col].str.replace(',', '.').astype(float)

    # Handle empty/NaN values in N00B column
    df['N00B'] = pd.to_numeric(df['N00B'], errors='coerce')

    # Calculate the virtual baseline (minimum of baseline and N00Balloc)
    df['virtual_baseline'] = df[['baseline', 'N00Balloc']].min(axis=1)

    # Calculate ratios, handling NaN values
    df['N00B_ratio'] = df['N00B'] / df['virtual_baseline']
    df['N00Balloc_ratio'] = df['N00Balloc'] / df['baseline']

    # Calculate geometric means (excluding NaN values)
    geomean_N00B = np.exp(np.mean(np.log(df['N00B_ratio'].dropna())))
    geomean_N00Balloc = np.exp(np.mean(np.log(df['N00Balloc_ratio'])))

    # Add geomean to the dataframe
    df.loc[len(df)] = ['geomean', 1.0, geomean_N00B, 1.0, 1.0, geomean_N00B, geomean_N00Balloc]

    # Plotting
    # Figure size - make it narrower and slightly taller to accommodate stacked content
    plt.figure(figsize=(8, 3))
    x = np.arange(len(df))
    width = 0.25  # Changed from 0.35

    # Create bars with colorblind-friendly colors AND hatches
    bars1 = plt.bar(x - width/2, df['N00Balloc_ratio'], width, 
                    label='N00Balloc', 
                    color='#004488')  # dark blue
    bars2 = plt.bar(x + width/2, df['N00B_ratio'], width, 
                    label='N00B', 
                    color='#EE7733')  # dark orange

    # Add value labels on top of each bar
    def autolabel(bars, ratios):
        for idx, (bar, ratio) in enumerate(zip(bars, ratios)):
            if not pd.isna(ratio):  # Only label if value exists
                height = bar.get_height()
                weight = 'bold' if idx == len(bars)-1 else 'normal'
                plt.text(bar.get_x() + bar.get_width()/2., height,
                        f'{height:.2f}',
                        ha='center',
                        va='bottom',
                        rotation=90,
                        family='monospace',
                        fontsize=10,
                        weight=weight)

    autolabel(bars1, df['N00Balloc_ratio'])
    autolabel(bars2, df['N00B_ratio'])

    # Bold the 'geomean' text in x-axis labels
    labels = df.iloc[:, 0].tolist()
    # Convert last label to bold
    labels[-1] = r'$\mathbf{geomean}$'

    plt.xlabel(df.columns[0], fontsize=11)  # Smaller x-axis label
    plt.ylabel('Run-Time Ratio', fontsize=11)  # Smaller y-axis label
    plt.xticks(x, labels, rotation=25, ha='right', fontsize=11)  # Apply updated labels
    plt.yticks(fontsize=11)  # Smaller y-axis ticks
    plt.grid(True, axis='y')

    # Add horizontal lines at y=1.0 and y=1.5
    plt.axhline(y=1.0, color='black', linestyle='-', alpha=0.2)
    plt.axhline(y=1.5, color='red', linestyle='--', alpha=0.3)  # Removed label parameter

    # Adjust y-axis to leave some space at top and bottom
    ymax = max(df['N00B_ratio'].max(), df['N00Balloc_ratio'].max())
    ymin = min(df['N00B_ratio'].min(), df['N00Balloc_ratio'].min())
    plt.ylim(ymin * 0.95, ymax * 1.1)  # 5% margin below, 10% margin above

    # Add specific tick at y=1.5
    yticks = plt.yticks()[0]  # Get current ticks
    if 1.5 not in yticks:
        plt.yticks(sorted(list(yticks) + [1.5]))  # Add 1.5 to existing ticks

    # Place legend at bottom right, below x-axis labels, with items side by side
    plt.legend(prop={'family': 'monospace', 'size': 11},
            loc='best',
            #   bbox_to_anchor=(0,0),
            ncol=2)  # Changed from 1 to 2 to place items side by side

    # Adjust bottom margin to make room for legend
    plt.subplots_adjust(bottom=0.3)

    # Save the plot as PDF with tight layout to include legend
    pdf_filename = f"{csv_filename}.pdf"
    plt.savefig(pdf_filename, bbox_inches='tight')
    plt.close()


if __name__ == '__main__':
    # Check if CSV filenames are provided
    if len(sys.argv) < 2:
        print("Usage: ./plot_results.py <csv_files...>")
        sys.exit(1)

    # Process each CSV file
    for csv_filename in sys.argv[1:]:
        print(f"Processing {csv_filename} ...")
        plot_results_from_csv(csv_filename)

