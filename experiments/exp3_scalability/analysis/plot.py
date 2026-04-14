#!/usr/bin/env python3
"""
EXP-3: 可扩展性测试可视化
"""

import sys
import os
import glob
import csv
import matplotlib.pyplot as plt
import numpy as np

def parse_results(results_dir):
    """解析结果文件"""
    data = []
    
    for csv_file in glob.glob(os.path.join(results_dir, "exp3_*.csv")):
        filename = os.path.basename(csv_file)
        
        # 解析: exp3v2_{tenants}t_{type}.csv
        parts = filename.replace('.csv', '').split('_')
        if len(parts) >= 3:
            tenants = int(parts[1].replace('t', ''))
            test_type = parts[2]  # baseline, quota5, quota50
            
            # 读取数据
            latencies = []
            total_time = 0
            total_success = 0
            
            with open(csv_file, 'r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    success = int(row['success'])
                    avg_lat = float(row['avg_latency_us'])
                    p50_lat = float(row['p50_latency_us'])
                    p99_lat = float(row['p99_latency_us'])
                    
                    total_success += success
                    total_time += float(row['total_time_us'])
                    latencies.append(avg_lat)
            
            if latencies:
                data.append({
                    'tenants': tenants,
                    'type': test_type,
                    'avg_latency': np.mean(latencies),
                    'p50_latency': np.median(latencies),
                    'p99_latency': np.percentile(latencies, 99),
                    'throughput': total_success / (total_time / 1e6) if total_time > 0 else 0
                })
    
    return data

def plot_latency_trend(data, output_dir):
    """绘制延迟随租户数变化趋势"""
    
    # 按类型分组
    baseline = sorted([d for d in data if d['type'] == 'baseline'], key=lambda x: x['tenants'])
    quota5 = sorted([d for d in data if d['type'] == 'quota5'], key=lambda x: x['tenants'])
    quota50 = sorted([d for d in data if d['type'] == 'quota50'], key=lambda x: x['tenants'])
    
    fig, axes = plt.subplots(1, 3, figsize=(15, 4))
    
    configs = [
        ('Average Latency', 'avg_latency', axes[0]),
        ('P50 Latency', 'p50_latency', axes[1]),
        ('P99 Latency', 'p99_latency', axes[2])
    ]
    
    for title, key, ax in configs:
        if baseline:
            x = [d['tenants'] for d in baseline]
            y = [d[key] for d in baseline]
            ax.plot(x, y, 'o-', label='No Intercept', linewidth=2, markersize=8)
        
        if quota5:
            x = [d['tenants'] for d in quota5]
            y = [d[key] for d in quota5]
            ax.plot(x, y, 's-', label='Quota=5', linewidth=2, markersize=8)
        
        if quota50:
            x = [d['tenants'] for d in quota50]
            y = [d[key] for d in quota50]
            ax.plot(x, y, '^-', label='Quota=50', linewidth=2, markersize=8)
        
        ax.set_xlabel('Number of Tenants')
        ax.set_ylabel(f'{title} (us)')
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale('log')
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'exp3_latency_trend.png'), dpi=150)
    plt.savefig(os.path.join(output_dir, 'exp3_latency_trend.pdf'))
    print("Saved: exp3_latency_trend.png")

def plot_throughput_comparison(data, output_dir):
    """绘制吞吐量对比"""
    baseline = sorted([d for d in data if d['type'] == 'baseline'], key=lambda x: x['tenants'])
    quota50 = sorted([d for d in data if d['type'] == 'quota50'], key=lambda x: x['tenants'])
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    x = np.arange(len(baseline))
    width = 0.35
    
    baseline_y = [d['throughput'] for d in baseline]
    quota50_y = [d['throughput'] for d in quota50] if quota50 else [0] * len(baseline)
    
    ax.bar(x - width/2, baseline_y, width, label='No Intercept', alpha=0.8)
    ax.bar(x + width/2, quota50_y, width, label='Quota=50', alpha=0.8)
    
    ax.set_xlabel('Number of Tenants')
    ax.set_ylabel('Throughput (ops/sec)')
    ax.set_title('EXP-3: Throughput Comparison (10 QP per tenant)')
    ax.set_xticks(x)
    ax.set_xticklabels([d['tenants'] for d in baseline])
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'exp3_throughput.png'), dpi=150)
    plt.savefig(os.path.join(output_dir, 'exp3_throughput.pdf'))
    print("Saved: exp3_throughput.png")

def print_summary(data):
    """打印汇总"""
    print("\n" + "="*80)
    print("EXP-3: Scalability Test Summary (Fixed 10 QP per tenant)")
    print("="*80)
    print(f"{'Tenants':<10} {'Type':<15} {'Avg Lat(us)':<15} {'P99 Lat(us)':<15} {'Throughput':<15}")
    print("-"*80)
    
    for d in sorted(data, key=lambda x: (x['type'], x['tenants'])):
        print(f"{d['tenants']:<10} {d['type']:<15} {d['avg_latency']:<15.1f} "
              f"{d['p99_latency']:<15.1f} {d['throughput']:<15.0f}")
    
    print("="*80)

def main():
    results_dir = sys.argv[1] if len(sys.argv) > 1 else "results"
    
    print(f"Loading results from: {results_dir}")
    data = parse_results(results_dir)
    
    if not data:
        print("No results found!")
        return 1
    
    print(f"Loaded {len(data)} test results")
    print_summary(data)
    
    plot_latency_trend(data, results_dir)
    plot_throughput_comparison(data, results_dir)
    
    print("\n✅ All plots generated!")
    return 0

if __name__ == '__main__':
    sys.exit(main())
