#! /usr/bin/python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys


def plot_results_from_csv(csv_filename):
    # Read the CSV file, forcing string type for numeric columns
    df = pd.read_csv(csv_filename)
    
    # Convert comma decimal separators to periods and convert to float
    for col in ['baseline', 'N00B', 'N00Balloc']:
        df[col] = pd.to_numeric(df[col].str.replace(',', '.'), errors='coerce')

    # Calculate ratios
    df['virtual_baseline'] = df[['baseline', 'N00Balloc']].min(axis=1)
    df['N00B_ratio'] = df['N00B'] / df['virtual_baseline']
    df['N00Balloc_ratio'] = df['N00Balloc'] / df['baseline']

        # Split into separate dataframes
    df_2006 = df[~df.iloc[:, 0].str.contains('_s|SPECspeed', na=False)].copy()  # No _s suffix and not the header
    df_2017 = df[df.iloc[:, 0].str.contains('_s', na=False)].copy()  # Only benchmarks with _s suffix
    
    # Calculate geometric means for each suite
    geomean_2006_N00B = np.exp(np.mean(np.log(df_2006['N00B_ratio'].dropna())))
    geomean_2006_N00Balloc = np.exp(np.mean(np.log(df_2006['N00Balloc_ratio'].dropna())))
    
    geomean_2017_N00B = np.exp(np.mean(np.log(df_2017['N00B_ratio'].dropna())))
    geomean_2017_N00Balloc = np.exp(np.mean(np.log(df_2017['N00Balloc_ratio'].dropna())))
    
    # Calculate overall geometric mean
    df_all = pd.concat([df_2006, df_2017])
    geomean_N00B = np.exp(np.mean(np.log(df_all['N00B_ratio'].dropna())))
    geomean_N00Balloc = np.exp(np.mean(np.log(df_all['N00Balloc_ratio'].dropna())))

    # Create result dataframe in desired order
    result_rows = []
    result_rows.extend(df_2006.to_dict('records'))
    result_rows.append({
        'SPEC CPU 2006': '2006 geomean',
        'baseline': np.nan,
        'N00B': np.nan,
        'N00Balloc': np.nan,
        'virtual_baseline': np.nan,
        'N00B_ratio': geomean_2006_N00B,
        'N00Balloc_ratio': geomean_2006_N00Balloc
    })
    result_rows.append({  # Empty row for spacing
        'SPEC CPU 2006': '',
        'baseline': np.nan,
        'N00B': np.nan,
        'N00Balloc': np.nan,
        'virtual_baseline': np.nan,
        'N00B_ratio': np.nan,
        'N00Balloc_ratio': np.nan
    })
    result_rows.extend(df_2017.to_dict('records'))
    result_rows.append({
        'SPEC CPU 2006': '2017 geomean',
        'baseline': np.nan,
        'N00B': np.nan,
        'N00Balloc': np.nan,
        'virtual_baseline': np.nan,
        'N00B_ratio': geomean_2017_N00B,
        'N00Balloc_ratio': geomean_2017_N00Balloc
    })
    result_rows.append({  # Empty row for spacing before overall geomean
        'SPEC CPU 2006': '',
        'baseline': np.nan,
        'N00B': np.nan,
        'N00Balloc': np.nan,
        'virtual_baseline': np.nan,
        'N00B_ratio': np.nan,
        'N00Balloc_ratio': np.nan
    })
    result_rows.append({
        'SPEC CPU 2006': 'overall geomean',
        'baseline': np.nan,
        'N00B': np.nan,
        'N00Balloc': np.nan,
        'virtual_baseline': np.nan,
        'N00B_ratio': geomean_N00B,
        'N00Balloc_ratio': geomean_N00Balloc
    })

    # Create final dataframe
    df = pd.DataFrame(result_rows)

    # Figure size
    plt.figure(figsize=(14, 3))

    x = np.arange(len(df))
    width = 0.35  # Increased from 0.25 to create more space between siblings
    spacing = 0.02  # Additional offset between bars

    # Create bars with colorblind-friendly colors AND hatches
    bars1 = plt.bar(x - (width/2 + spacing), df['N00Balloc_ratio'], width, 
                    label='N00Balloc', 
                    color='#004488')  # dark blue
    bars2 = plt.bar(x + (width/2 + spacing), df['N00B_ratio'], width, 
                    label='N00B', 
                    color='#EE7733')  # dark orange

    # Set x-axis limits with a bit more padding
    plt.xlim(-0.75, len(df) - 0.25)  # Changed from -0.5 and -0.5 to add more space

    # Add value labels on top of each bar
    def autolabel(bars, ratios):
        for idx, (bar, ratio) in enumerate(zip(bars, ratios)):
            if not pd.isna(ratio):  # Only label if value exists
                height = bar.get_height()
                weight = 'bold' if 'geomean' in str(df.iloc[idx, 0]) else 'normal'
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

    # Bold all geomean labels in x-axis labels
    labels = df.iloc[:, 0].tolist()
    for i, label in enumerate(labels):
        if 'geomean' in str(label):
            labels[i] = r'$\mathbf{' + label.replace(' ', '\ ') + '}$'

    # plt.xlabel(df.columns[0], fontsize=11)  # Smaller x-axis label
    plt.ylabel('Run-Time Ratio', fontsize=11)  # Smaller y-axis label
    # Adjust x-axis label positioning
    plt.xticks(x, labels, rotation=35, ha='right', fontsize=11)  # Added x offset
    plt.tick_params(axis='x', which='major', pad=0) # Remove padding between ticks and labels
    plt.yticks(fontsize=11)  # Smaller y-axis ticks
    plt.tick_params(axis='y', which='major', pad=0) # Remove padding between ticks and labels
    plt.grid(True, axis='y')

    # Add horizontal lines at y=1.0 and y=1.5
    plt.axhline(y=1.0, color='black', linestyle='-', alpha=0.2)
    plt.axhline(y=1.5, color='red', linestyle='--', alpha=0.3)  # Removed label parameter

    # Adjust y-axis to leave proportional space at top and bottom
    ymax = max(df['N00B_ratio'].max(), df['N00Balloc_ratio'].max())
    ymin = min(df['N00B_ratio'].min(), df['N00Balloc_ratio'].min())
    
    # Calculate space needed for bar labels
    label_height = 0.3 
    
    plt.ylim(ymin * 0.95, ymax + label_height)

    # Add specific tick at y=1.5
    yticks = plt.yticks()[0]  # Get current ticks
    if 1.5 not in yticks:
        plt.yticks(sorted(list(yticks) + [1.5]))

    # Place legend at bottom right, below x-axis labels, with items side by side
    plt.legend(prop={'family': 'monospace', 'size': 11},
            loc='upper right',
            bbox_to_anchor=(1,1.16),
            ncol=2)  # Changed from 1 to 2 to place items side by side

    # Save the plot as PDF with tight layout to include legend
    pdf_filename = f"{csv_filename}.pdf"
    plt.savefig(pdf_filename, bbox_inches='tight', pad_inches=0)
    print(f"Saved to {pdf_filename}")
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

