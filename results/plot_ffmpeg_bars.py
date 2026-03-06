#! /usr/bin/python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import sys

if len(sys.argv) < 2:
    print("Usage: python3 plot_ffmpeg_bars.py <input_csv>")
    sys.exit(1)

csv_path = sys.argv[1]
output_path = os.path.splitext(csv_path)[0] + ".pdf"

# Read the CSV
df = pd.read_csv(csv_path)

# Function to parse numbers with commas
def parse_float(x):
    if isinstance(x, str):
        return float(x.replace(',', '.'))
    return float(x)

# Process the data
# First column is the scenario label
labels = df.iloc[:, 0].astype(str).str.replace('libx264 ', '')

# Extract and convert data series
baseline = df['baseline'].apply(parse_float)
full_noob = df['full N00B'].apply(parse_float)
libavformat = df['libavformat'].apply(parse_float)

# Setup plotting
x = np.arange(len(labels))
width = 0.25

fig, ax = plt.subplots(figsize=(6, 3))

rects1 = ax.bar(x - width, baseline, width, label='Baseline')
rects2 = ax.bar(x, full_noob, width, label='Full N00B')
rects3 = ax.bar(x + width, libavformat, width, label='libavformat')

ax.set_ylabel('FPS')
ax.set_title(f'FFmpeg libx264')
ax.set_xticks(x)
ax.set_xticklabels(labels)
ax.legend(frameon=False)

# Remove top and right spines
ax.spines['top'].set_visible(False)
ax.spines['right'].set_visible(False)

def autolabel(rects):
    for rect in rects:
        height = rect.get_height()
        ax.annotate(f'{height:.1f}',
                    xy=(rect.get_x() + rect.get_width() / 2, height),
                    xytext=(0, 3),
                    textcoords="offset points",
                    ha='center', va='bottom')

autolabel(rects1)
autolabel(rects2)
autolabel(rects3)

plt.tight_layout()
plt.savefig(output_path)
print(f"Chart saved to {output_path}")
