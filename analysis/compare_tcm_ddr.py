import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter
import sys

# Timer runs at 100 MHz
TIMER_FREQ_MHZ = 100.0

def load_and_compute_real_latency(filename, label):
    """Load CSV and calculate the actual latency from the timestamps."""
    print(f"\nLoading {label} data from {filename}...")
    
    df = pd.read_csv(filename)
    
    # Real latency is just the difference between RPU receiving and APU sending
    df['real_latency_ticks'] = df['rpu_timestamp'] - df['apu_timestamp']
    df['real_latency_us'] = df['real_latency_ticks'] / TIMER_FREQ_MHZ
    
    # Drop anything that looks wrong (negative latencies shouldn't happen)
    df = df[df['real_latency_us'] > 0]
    
    print(f"  Loaded {len(df)} samples")
    print(f"  Latency range: {df['real_latency_us'].min():.3f} - {df['real_latency_us'].max():.3f} µs")
    
    return df

def compute_stats_by_size(df):
    """Group by packet size and calculate basic statistics."""
    stats = df.groupby('packet_size')['real_latency_us'].agg([
        ('mean', 'mean'),
        ('std', 'std'),
        ('min', 'min'),
        ('max', 'max'),
        ('median', 'median'),
        ('count', 'count'),
        ('q25', lambda x: x.quantile(0.25)),
        ('q75', lambda x: x.quantile(0.75))
    ]).reset_index()
    
    # Coefficient of variation gives us relative jitter
    stats['cv'] = (stats['std'] / stats['mean']) * 100
    
    return stats

def print_comparison(tcm_stats, ddr_stats):
    """Print a nice comparison table between TCM and DDR results."""
    print("\n" + "="*90)
    print("TCM vs DDR LATENCY COMPARISON (Real Latency from Timestamps)")
    print("="*90)
    print(f"\n{'Size':<10} {'TCM Mean':<12} {'TCM Std':<12} {'DDR Mean':<12} {'DDR Std':<12} {'Speedup':<10}")
    print(f"{'(bytes)':<10} {'(µs)':<12} {'(µs)':<12} {'(µs)':<12} {'(µs)':<12} {'(TCM/DDR)':<10}")
    print("-"*90)
    
    # Merge the two datasets on packet size
    merged = pd.merge(tcm_stats, ddr_stats, on='packet_size', suffixes=('_tcm', '_ddr'))
    
    for _, row in merged.iterrows():
        size = int(row['packet_size'])
        size_str = f"{size}" if size < 1024 else f"{size//1024}K"
        
        speedup = row['mean_ddr'] / row['mean_tcm']
        
        print(f"{size_str:<10} {row['mean_tcm']:<12.3f} {row['std_tcm']:<12.3f} "
              f"{row['mean_ddr']:<12.3f} {row['std_ddr']:<12.3f} {speedup:<10.1f}x")
    
    print("\n" + "="*90)
    print("KEY FINDINGS:")
    print("="*90)
    
    # Overall averages
    avg_tcm = merged['mean_tcm'].mean()
    avg_ddr = merged['mean_ddr'].mean()
    overall_speedup = avg_ddr / avg_tcm
    
    print(f"\n1. Average TCM latency: {avg_tcm:.3f} µs")
    print(f"2. Average DDR latency: {avg_ddr:.3f} µs")
    print(f"3. Overall TCM speedup: {overall_speedup:.1f}x faster than DDR")
    
    # Jitter comparison
    avg_tcm_std = merged['std_tcm'].mean()
    avg_ddr_std = merged['std_ddr'].mean()
    print(f"\n4. Average TCM std dev: {avg_tcm_std:.3f} µs (CV: {merged['cv_tcm'].mean():.1f}%)")
    print(f"5. Average DDR std dev: {avg_ddr_std:.3f} µs (CV: {merged['cv_ddr'].mean():.1f}%)")
    
    if avg_tcm_std > 0:
        consistency = avg_ddr_std / avg_tcm_std
        print(f"6. TCM is {consistency:.1f}x more consistent (lower jitter)")
    else:
        print(f"6. TCM has near-zero jitter (extremely consistent!)")
    
    print("\n" + "="*90)
    
    return merged

def plot_comparison(merged, tcm_df, ddr_df):
    """Generate the comparison plots (4 subplots)."""
    print("\nGenerating comparison plots...")
    
    fig, axes = plt.subplots(2, 2, figsize=(14, 12))
    fig.suptitle('TCM vs DDR Performance Comparison\n(Real Latency from Timestamps)', 
                 fontsize=14, fontweight='bold')
    
    packet_sizes = merged['packet_size'].values
    
    # Top-left: Mean latency with error bars
    ax1 = axes[0, 0]
    ax1.errorbar(packet_sizes, merged['mean_tcm'], yerr=merged['std_tcm'],
                 fmt='bo-', capsize=5, linewidth=2, markersize=8, 
                 label='TCM', elinewidth=2)
    ax1.errorbar(packet_sizes, merged['mean_ddr'], yerr=merged['std_ddr'],
                 fmt='rs-', capsize=5, linewidth=2, markersize=8,
                 label='DDR', elinewidth=2)
    
    ax1.set_xscale('log', base=2)
    # Keeping y-axis linear so TCM values are actually visible
    ax1.set_xlabel('Packet Size (bytes)', fontsize=11)
    ax1.set_ylabel('Latency (µs)', fontsize=11)
    ax1.set_title('Latency vs Packet Size', fontsize=12, fontweight='bold')
    ax1.legend(loc='upper left', fontsize=10)
    ax1.grid(True, which='both', linestyle='--', alpha=0.7)
    ax1.xaxis.set_major_formatter(ScalarFormatter())
    
    # Top-right: Speedup bars
    ax2 = axes[0, 1]
    speedup = merged['mean_ddr'] / merged['mean_tcm']
    bars = ax2.bar(range(len(packet_sizes)), speedup, 
                   color='green', alpha=0.7, edgecolor='black')
    
    # Add speedup values on top of bars
    for i, (bar, val) in enumerate(zip(bars, speedup)):
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height,
                f'{val:.1f}x', ha='center', va='bottom', fontsize=9)
    
    ax2.set_xticks(range(len(packet_sizes)))
    ax2.set_xticklabels([f"{s}" if s < 1024 else f"{s//1024}K" 
                          for s in packet_sizes], rotation=45)
    ax2.set_xlabel('Packet Size (bytes)', fontsize=11)
    ax2.set_ylabel('TCM Speedup (DDR/TCM)', fontsize=11)
    ax2.set_title('TCM Speedup over DDR', fontsize=12, fontweight='bold')
    ax2.axhline(y=1.0, color='red', linestyle='--', linewidth=2, label='Equal performance')
    ax2.grid(True, axis='y', linestyle='--', alpha=0.7)
    ax2.legend(loc='upper right')
    
    # Bottom-left: Jitter comparison using coefficient of variation
    ax3 = axes[1, 0]
    x = np.arange(len(packet_sizes))
    width = 0.35
    
    bars1 = ax3.bar(x - width/2, merged['cv_tcm'], width, 
                    label='TCM', color='blue', alpha=0.7)
    bars2 = ax3.bar(x + width/2, merged['cv_ddr'], width,
                    label='DDR', color='red', alpha=0.7)
    
    ax3.set_xticks(x)
    ax3.set_xticklabels([f"{s}" if s < 1024 else f"{s//1024}K" 
                         for s in packet_sizes], rotation=45)
    ax3.set_xlabel('Packet Size (bytes)', fontsize=11)
    ax3.set_ylabel('Coefficient of Variation (%)', fontsize=11)
    ax3.set_title('Jitter Comparison (Lower is Better)', fontsize=12, fontweight='bold')
    ax3.legend(loc='upper right')
    ax3.grid(True, axis='y', linestyle='--', alpha=0.7)
    
    # Bottom-right: Distribution histogram for 1KB packets
    ax4 = axes[1, 1]
    
    target_size = 1024
    if target_size in packet_sizes:
        tcm_1k = tcm_df[tcm_df['packet_size'] == target_size]['real_latency_us']
        ddr_1k = ddr_df[ddr_df['packet_size'] == target_size]['real_latency_us']
        
        ax4.hist(tcm_1k, bins=30, alpha=0.5, label='TCM', color='blue', edgecolor='black')
        ax4.hist(ddr_1k, bins=30, alpha=0.5, label='DDR', color='red', edgecolor='black')
        
        ax4.set_xlabel('Latency (µs)', fontsize=11)
        ax4.set_ylabel('Frequency', fontsize=11)
        ax4.set_title(f'Latency Distribution (Packet Size = {target_size} bytes)', 
                     fontsize=12, fontweight='bold')
        ax4.legend(loc='upper right')
        ax4.grid(True, axis='y', linestyle='--', alpha=0.7)
    else:
        ax4.text(0.5, 0.5, 'No 1KB data available', 
                ha='center', va='center', transform=ax4.transAxes)
    
    plt.tight_layout()
    
    # Save both PNG and PDF versions
    plt.savefig('tcm_vs_ddr_comparison.png', dpi=300, bbox_inches='tight')
    print("Saved: tcm_vs_ddr_comparison.png")
    
    plt.savefig('tcm_vs_ddr_comparison.pdf', bbox_inches='tight')
    print("Saved: tcm_vs_ddr_comparison.pdf")
    
    plt.close()

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 compare_tcm_ddr.py <tcm_results.csv> <ddr_results.csv>")
        sys.exit(1)
    
    tcm_file = sys.argv[1]
    ddr_file = sys.argv[2]
    
    # Load both datasets and compute real latencies
    tcm_df = load_and_compute_real_latency(tcm_file, "TCM")
    ddr_df = load_and_compute_real_latency(ddr_file, "DDR")
    
    # Calculate stats for each packet size
    tcm_stats = compute_stats_by_size(tcm_df)
    ddr_stats = compute_stats_by_size(ddr_df)
    
    # Print the comparison table
    merged = print_comparison(tcm_stats, ddr_stats)
    
    # Generate the plots
    plot_comparison(merged, tcm_df, ddr_df)
    
    # Export merged statistics to CSV
    merged.to_csv('tcm_vs_ddr_stats.csv', index=False)
    print("\nSaved statistics to: tcm_vs_ddr_stats.csv")
    
    print("\n✅ Analysis complete!")

if __name__ == "__main__":
    main()