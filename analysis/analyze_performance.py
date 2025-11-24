#!/usr/bin/env python3
"""
Performance Analysis and Plotting Script for APU-RPU Communication

This script takes the CSV results from our performance experiments and
generates plots comparing what we measured (non-coherent) vs what we'd
theoretically get with cache coherence enabled.

Usage:
    python3 analyze_performance.py performance_results.csv
    python3 analyze_performance.py performance_results.csv --output-prefix experiment1
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter
import argparse
import sys
from pathlib import Path

# Timer configuration
TIMER_FREQ_MHZ = 100.0  # MHz
TIMER_FREQ_HZ = 100_000_000  # Hz

# System parameters for our theoretical model
# DDR4 bandwidth (theoretical max is around 25 GB/s, but in practice we see more like 15-20 GB/s)
DDR4_BANDWIDTH_GBS = 15.0  # GB/s (being conservative here)
DDR4_BANDWIDTH_BS = DDR4_BANDWIDTH_GBS * 1e9  # convert to bytes/s

# Cache line size
CACHE_LINE_SIZE = 64  # bytes

# Latency estimates (in microseconds)
# These are rough estimates based on ARM Cortex-A53/R5F specs
DRAM_ACCESS_LATENCY_NS = 100  # ns per DRAM access
CACHE_HIT_LATENCY_NS = 10     # ns for L1 cache hit
CCI_SNOOP_LATENCY_NS = 50     # ns for CCI snoop operation
SOFTWARE_OVERHEAD_NS = 200    # ns for function calls, polling, etc.


def load_data(filename):
    """Load and validate the CSV data."""
    print(f"Loading data from {filename}...")
    
    try:
        df = pd.read_csv(filename)
    except Exception as e:
        print(f"Error loading file: {e}")
        sys.exit(1)
    
    # Make sure we have all the columns we need
    required_cols = ['packet_size', 'apu_timestamp', 'rpu_timestamp', 
                     'delta_ticks', 'delta_us']
    
    for col in required_cols:
        if col not in df.columns:
            print(f"Error: Missing column '{col}'")
            sys.exit(1)
    
    # Filter out any negative deltas (would indicate timer rollover or something went wrong)
    initial_count = len(df)
    df = df[df['delta_us'] > 0]
    filtered_count = len(df)
    
    if filtered_count < initial_count:
        print(f"Warning: Filtered out {initial_count - filtered_count} invalid entries")
    
    print(f"Loaded {filtered_count} valid measurements")
    return df


def compute_statistics(df):
    """Compute statistics for each packet size."""
    print("\nComputing statistics...")
    
    # Group by packet size and calculate all the stats we care about
    stats = df.groupby('packet_size')['delta_us'].agg([
        ('mean', 'mean'),
        ('std', 'std'),
        ('min', 'min'),
        ('max', 'max'),
        ('median', 'median'),
        ('count', 'count'),
        ('q25', lambda x: x.quantile(0.25)),
        ('q75', lambda x: x.quantile(0.75))
    ]).reset_index()
    
    # Calculate coefficient of variation (useful for seeing how noisy the measurements are)
    stats['cv'] = stats['std'] / stats['mean'] * 100  # as a percentage
    
    return stats


def theoretical_coherent_time(packet_size):
    """
    Calculate theoretical transfer time WITH CCI-400 cache coherence enabled.
    
    In a cache-coherent scenario:
    1. APU writes data (goes to cache, marked dirty)
    2. APU signals RPU (cache line write + flush OR hardware coherence)
    3. CCI-400 handles coherence (snoop, cache-to-cache transfer)
    4. RPU reads data (from cache or via CCI snoop)
    
    Benefits:
    - No explicit cache flush operations needed
    - Direct cache-to-cache transfers for small data
    - Way less software overhead
    """
    # Base latency (this is always there regardless)
    base_latency_us = SOFTWARE_OVERHEAD_NS / 1000.0
    
    # Small packets (< 2 cache lines): dominated by snoop latency
    if packet_size <= 2 * CACHE_LINE_SIZE:
        # Direct cache-to-cache transfer via CCI
        num_lines = max(1, (packet_size + CACHE_LINE_SIZE - 1) // CACHE_LINE_SIZE)
        snoop_time_us = (CCI_SNOOP_LATENCY_NS * num_lines) / 1000.0
        return base_latency_us + snoop_time_us
    
    # Medium packets (2-16 cache lines): mix of snoop and bandwidth
    elif packet_size <= 16 * CACHE_LINE_SIZE:
        num_lines = (packet_size + CACHE_LINE_SIZE - 1) // CACHE_LINE_SIZE
        snoop_time_us = (CCI_SNOOP_LATENCY_NS * num_lines) / 1000.0
        # Bandwidth component starts to matter here
        bw_time_us = (packet_size / DDR4_BANDWIDTH_BS) * 1e6
        return base_latency_us + max(snoop_time_us, bw_time_us)
    
    # Large packets: dominated by bandwidth
    else:
        # Fixed snoop overhead for cache line invalidation
        snoop_overhead_us = (CCI_SNOOP_LATENCY_NS * 10) / 1000.0  # ~10 lines overhead
        # Bandwidth limited
        bw_time_us = (packet_size / DDR4_BANDWIDTH_BS) * 1e6
        return base_latency_us + snoop_overhead_us + bw_time_us


def theoretical_non_coherent_time(packet_size):
    """
    Calculate theoretical transfer time WITHOUT cache coherence.
    
    In non-coherent scenario:
    1. APU writes data to cache (or directly to DRAM with O_SYNC)
    2. APU explicitly flushes cache (if using cached memory)
    3. APU signals RPU
    4. RPU invalidates cache (to ensure fresh read)
    5. RPU reads from DRAM
    
    Overheads:
    - Explicit cache flush operations
    - Forced DRAM accesses (no cache-to-cache)
    - Higher software overhead for cache management
    """
    # Base latency (higher because of cache management overhead)
    base_latency_us = (SOFTWARE_OVERHEAD_NS + 300) / 1000.0  # Extra overhead
    
    # Cache flush cost (proportional to data size)
    num_lines = max(1, (packet_size + CACHE_LINE_SIZE - 1) // CACHE_LINE_SIZE)
    flush_time_us = (DRAM_ACCESS_LATENCY_NS * num_lines) / 1000.0
    
    # DRAM read cost (RPU has to read from DRAM, not cache)
    dram_read_time_us = flush_time_us  # Similar cost
    
    # Bandwidth component for larger transfers
    bw_time_us = (packet_size / DDR4_BANDWIDTH_BS) * 1e6
    
    # Total: base + flush + read + bandwidth (whichever dominates)
    return base_latency_us + flush_time_us + max(dram_read_time_us, bw_time_us)


def plot_results(stats, output_prefix="perf"):
    """Generate comprehensive plots of the results."""
    print("\nGenerating plots...")
    
    # Create a figure with 4 subplots to show different aspects
    fig, axes = plt.subplots(2, 2, figsize=(14, 12))
    fig.suptitle('APU-RPU Communication Performance Analysis\n(Non-Coherent Scenario)', 
                 fontsize=14, fontweight='bold')
    
    packet_sizes = stats['packet_size'].values
    measured_mean = stats['mean'].values
    measured_std = stats['std'].values
    
    # Calculate theoretical values for both scenarios
    theory_coherent = np.array([theoretical_coherent_time(s) for s in packet_sizes])
    theory_non_coherent = np.array([theoretical_non_coherent_time(s) for s in packet_sizes])
    
    # Plot 1: Main comparison plot (log-log scale)
    ax1 = axes[0, 0]
    ax1.errorbar(packet_sizes, measured_mean, yerr=measured_std, 
                 fmt='bo-', capsize=5, capthick=2, linewidth=2, markersize=8,
                 label='Measured (Non-Coherent)', elinewidth=2)
    ax1.plot(packet_sizes, theory_coherent, 'g--', linewidth=2, markersize=8,
             marker='s', label='Theoretical (Coherent/CCI-400)')
    ax1.plot(packet_sizes, theory_non_coherent, 'r:', linewidth=2, markersize=8,
             marker='^', label='Theoretical (Non-Coherent)')
    
    ax1.set_xscale('log', base=2)
    ax1.set_yscale('log')
    ax1.set_xlabel('Packet Size (bytes)', fontsize=11)
    ax1.set_ylabel('Transfer Time (µs)', fontsize=11)
    ax1.set_title('Transfer Time vs Packet Size', fontsize=12, fontweight='bold')
    ax1.legend(loc='upper left', fontsize=10)
    ax1.grid(True, which='both', linestyle='--', alpha=0.7)
    ax1.xaxis.set_major_formatter(ScalarFormatter())
    
    # Add some reference lines for packet sizes
    for size in packet_sizes:
        if size >= 1024:
            label = f"{size//1024}K"
        else:
            label = str(size)
        ax1.axvline(x=size, color='gray', linestyle=':', alpha=0.3)
    
    # Plot 2: Speedup comparison
    ax2 = axes[0, 1]
    speedup_potential = measured_mean / theory_coherent
    bars = ax2.bar(range(len(packet_sizes)), speedup_potential, 
                   color='steelblue', alpha=0.7, edgecolor='black')
    
    # Add value labels on top of bars
    for i, (bar, val) in enumerate(zip(bars, speedup_potential)):
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height,
                f'{val:.1f}x', ha='center', va='bottom', fontsize=9)
    
    ax2.set_xticks(range(len(packet_sizes)))
    ax2.set_xticklabels([f"{s}" if s < 1024 else f"{s//1024}K" 
                          for s in packet_sizes], rotation=45)
    ax2.set_xlabel('Packet Size (bytes)', fontsize=11)
    ax2.set_ylabel('Potential Speedup', fontsize=11)
    ax2.set_title('Potential Speedup with CCI-400 Coherence', fontsize=12, fontweight='bold')
    ax2.axhline(y=1.0, color='red', linestyle='--', linewidth=2, label='No improvement')
    ax2.grid(True, axis='y', linestyle='--', alpha=0.7)
    ax2.legend(loc='upper right')
    
    # Plot 3: Coefficient of Variation (shows how consistent our measurements are)
    ax3 = axes[1, 0]
    bars = ax3.bar(range(len(packet_sizes)), stats['cv'].values,
                   color='coral', alpha=0.7, edgecolor='black')
    
    # Highlight measurements with high jitter
    for i, (bar, cv) in enumerate(zip(bars, stats['cv'].values)):
        if cv > 10:  # High jitter threshold
            bar.set_color('red')
            bar.set_alpha(0.8)
    
    ax3.set_xticks(range(len(packet_sizes)))
    ax3.set_xticklabels([f"{s}" if s < 1024 else f"{s//1024}K" 
                          for s in packet_sizes], rotation=45)
    ax3.set_xlabel('Packet Size (bytes)', fontsize=11)
    ax3.set_ylabel('Coefficient of Variation (%)', fontsize=11)
    ax3.set_title('Measurement Jitter (Lower is Better)', fontsize=12, fontweight='bold')
    ax3.axhline(y=10.0, color='orange', linestyle='--', linewidth=2, 
                label='High jitter threshold')
    ax3.grid(True, axis='y', linestyle='--', alpha=0.7)
    ax3.legend(loc='upper right')
    
    # Plot 4: Throughput analysis
    ax4 = axes[1, 1]
    # Calculate effective throughput in MB/s
    throughput_mbs = (packet_sizes / measured_mean) / 1.0  # bytes/µs = MB/s
    
    ax4.plot(packet_sizes, throughput_mbs, 'mo-', linewidth=2, markersize=8,
             label='Measured Throughput')
    
    # Show theoretical maximum as a reference
    max_throughput = DDR4_BANDWIDTH_GBS * 1000  # Convert to MB/s
    ax4.axhline(y=max_throughput, color='green', linestyle='--', linewidth=2,
                label=f'DDR4 Max ({DDR4_BANDWIDTH_GBS:.0f} GB/s)')
    
    ax4.set_xscale('log', base=2)
    ax4.set_yscale('log')
    ax4.set_xlabel('Packet Size (bytes)', fontsize=11)
    ax4.set_ylabel('Throughput (MB/s)', fontsize=11)
    ax4.set_title('Effective Throughput', fontsize=12, fontweight='bold')
    ax4.legend(loc='lower right', fontsize=10)
    ax4.grid(True, which='both', linestyle='--', alpha=0.7)
    ax4.xaxis.set_major_formatter(ScalarFormatter())
    
    plt.tight_layout()
    
    # Save as PNG for presentations
    plot_file = f"{output_prefix}_analysis.png"
    plt.savefig(plot_file, dpi=300, bbox_inches='tight')
    print(f"Saved plot to {plot_file}")
    
    # Also save as PDF for LaTeX documents
    pdf_file = f"{output_prefix}_analysis.pdf"
    plt.savefig(pdf_file, bbox_inches='tight')
    print(f"Saved PDF to {pdf_file}")
    
    plt.close()


def generate_latex_table(stats):
    """Generate LaTeX table for documentation."""
    print("\nGenerating LaTeX table...")
    
    latex = r"""
\begin{table}[H]
\centering
\caption{APU-RPU Communication Performance Results (Non-Coherent Scenario)}
\label{tab:perf_results}
\begin{tabular}{@{}rrrrrrr@{}}
\toprule
\textbf{Size} & \textbf{Mean} & \textbf{Std} & \textbf{Min} & \textbf{Max} & \textbf{CV} & \textbf{Speedup} \\
\textbf{(bytes)} & \textbf{(µs)} & \textbf{(µs)} & \textbf{(µs)} & \textbf{(µs)} & \textbf{(\%)} & \textbf{Potential} \\
\midrule
"""
    
    for _, row in stats.iterrows():
        size = int(row['packet_size'])
        if size >= 1024:
            size_str = f"{size//1024} KB"
        else:
            size_str = f"{size}"
        
        theory_coh = theoretical_coherent_time(row['packet_size'])
        speedup = row['mean'] / theory_coh
        
        latex += f"{size_str} & {row['mean']:.3f} & {row['std']:.3f} & "
        latex += f"{row['min']:.3f} & {row['max']:.3f} & {row['cv']:.1f} & {speedup:.1f}x \\\\\n"
    
    latex += r"""
\bottomrule
\end{tabular}
\end{table}
"""
    return latex


def print_summary(stats):
    """Print summary statistics to console."""
    print("\n" + "="*70)
    print("PERFORMANCE MEASUREMENT SUMMARY")
    print("="*70)
    
    print(f"\n{'Size (bytes)':<15} {'Mean (µs)':<12} {'Std (µs)':<12} "
          f"{'CV (%)':<10} {'Speedup':<10}")
    print("-"*70)
    
    for _, row in stats.iterrows():
        size = int(row['packet_size'])
        if size >= 1024:
            size_str = f"{size//1024} KB"
        else:
            size_str = str(size)
        
        theory_coh = theoretical_coherent_time(row['packet_size'])
        speedup = row['mean'] / theory_coh
        
        print(f"{size_str:<15} {row['mean']:<12.3f} {row['std']:<12.3f} "
              f"{row['cv']:<10.1f} {speedup:<10.1f}x")
    
    print("\n" + "="*70)
    print("KEY FINDINGS:")
    print("="*70)
    
    # Find the best and worst cases
    best_cv_idx = stats['cv'].idxmin()
    worst_cv_idx = stats['cv'].idxmax()
    
    print(f"\n1. Most consistent measurement: {int(stats.loc[best_cv_idx, 'packet_size'])} bytes "
          f"(CV = {stats.loc[best_cv_idx, 'cv']:.1f}%)")
    print(f"2. Highest jitter: {int(stats.loc[worst_cv_idx, 'packet_size'])} bytes "
          f"(CV = {stats.loc[worst_cv_idx, 'cv']:.1f}%)")
    
    # Calculate average potential speedup with CCI-400
    speedups = [stats.loc[i, 'mean'] / theoretical_coherent_time(stats.loc[i, 'packet_size']) 
                for i in stats.index]
    avg_speedup = np.mean(speedups)
    max_speedup = np.max(speedups)
    
    print(f"3. Average potential speedup with CCI-400: {avg_speedup:.1f}x")
    print(f"4. Maximum potential speedup: {max_speedup:.1f}x")
    
    print("\n" + "="*70)


def main():
    parser = argparse.ArgumentParser(description='Analyze APU-RPU performance data')
    parser.add_argument('csv_file', help='Input CSV file with performance data')
    parser.add_argument('--output-prefix', default='perf', 
                        help='Prefix for output files (default: perf)')
    parser.add_argument('--latex', action='store_true',
                        help='Generate LaTeX table')
    
    args = parser.parse_args()
    
    # Load the data from CSV
    df = load_data(args.csv_file)
    
    # Compute all the statistics
    stats = compute_statistics(df)
    
    # Print summary to console
    print_summary(stats)
    
    # Generate the plots
    plot_results(stats, args.output_prefix)
    
    # Save statistics to a new CSV file
    stats_file = f"{args.output_prefix}_statistics.csv"
    stats.to_csv(stats_file, index=False)
    print(f"\nSaved statistics to {stats_file}")
    
    # Generate LaTeX table if requested
    if args.latex:
        latex_table = generate_latex_table(stats)
        latex_file = f"{args.output_prefix}_table.tex"
        with open(latex_file, 'w') as f:
            f.write(latex_table)
        print(f"Saved LaTeX table to {latex_file}")
    
    print("\nAnalysis complete!")


if __name__ == "__main__":
    main()