#!/usr/bin/env python3
"""
EXP-4: 缓存性能评估 - 绘图脚本（不使用pandas）
"""

import os
import sys
import matplotlib.pyplot as plt
import numpy as np
from glob import glob

def load_result_csv(filepath):
    """加载单个结果CSV文件"""
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
        
        # 解析配置（第2行）
        config = {}
        if len(lines) >= 2:
            parts = lines[1].strip().split(',')
            if len(parts) >= 6:
                config = {
                    'test_type': int(parts[0]),
                    'workload_type': int(parts[1]),
                    'use_cache': int(parts[2]),
                    'adaptive_ttl': int(parts[3]),
                    'num_processes': int(parts[4]),
                    'ttl_ms': int(parts[5])
                }
        
        # 解析指标（从第5行开始）
        metrics = {}
        for line in lines[4:]:
            line = line.strip()
            if line and ',' in line:
                parts = line.split(',')
                if len(parts) >= 2:
                    metric_name = parts[0]
                    try:
                        metric_value = float(parts[1])
                        metrics[metric_name] = metric_value
                    except:
                        pass
        
        return {**config, **metrics}
    except Exception as e:
        print(f"Error loading {filepath}: {e}")
        return None

def plot_hit_rate(results_dir):
    """绘制缓存命中率图表"""
    hitrate_files = glob(os.path.join(results_dir, 'exp4_hitrate_*.csv'))
    
    if not hitrate_files:
        print("No hit rate results found")
        return
    
    data = []
    for f in hitrate_files:
        result = load_result_csv(f)
        if result:
            filename = os.path.basename(f)
            if 'seq' in filename:
                workload = 'Sequential'
            elif 'random' in filename:
                workload = 'Random'
            elif 'temporal' in filename:
                workload = 'Temporal'
            else:
                workload = 'Unknown'
            
            result['workload'] = workload
            data.append(result)
    
    if not data:
        print("No valid hit rate data")
        return
    
    # 提取数据
    workloads = list(set([d['workload'] for d in data]))
    ttls = sorted(set([d['ttl_ms'] for d in data]))
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    x = np.arange(len(workloads))
    width = 0.25
    colors = ['#2ecc71', '#3498db', '#e74c3c']
    
    for i, ttl in enumerate(ttls):
        hit_rates = []
        for wl in workloads:
            found = False
            for d in data:
                if d['ttl_ms'] == ttl and d['workload'] == wl:
                    hit_rates.append(d['hit_rate'])
                    found = True
                    break
            if not found:
                hit_rates.append(0)
        
        offset = (i - 1) * width
        bars = ax.bar(x + offset, hit_rates, width, 
                     label=f'TTL={int(ttl)}ms', color=colors[i % len(colors)], 
                     edgecolor='black')
        
        for bar, val in zip(bars, hit_rates):
            height = bar.get_height()
            ax.annotate(f'{val:.1f}%',
                       xy=(bar.get_x() + bar.get_width() / 2, height),
                       xytext=(0, 3),
                       textcoords="offset points",
                       ha='center', va='bottom', fontsize=8)
    
    ax.set_ylabel('Hit Rate (%)', fontsize=12)
    ax.set_title('EXP-4: Cache Hit Rate by Workload', fontsize=14, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels(workloads)
    ax.legend(title='TTL Setting')
    ax.set_ylim([0, 105])
    ax.grid(axis='y', alpha=0.3)
    ax.axhline(y=90, color='red', linestyle='--', linewidth=1.5, alpha=0.7)
    
    plt.tight_layout()
    output = os.path.join(results_dir, 'exp4_hitrate.png')
    plt.savefig(output, dpi=150)
    plt.close()
    print(f"Saved: {output}")

def plot_latency_comparison(results_dir):
    """绘制延迟对比图表"""
    nocache_file = os.path.join(results_dir, 'exp4_latency_nocache.csv')
    cache_files = glob(os.path.join(results_dir, 'exp4_latency_cache*.csv'))
    
    data = []
    
    if os.path.exists(nocache_file):
        result = load_result_csv(nocache_file)
        if result:
            result['mode'] = 'No Cache (SHM)'
            data.append(result)
    
    for f in cache_files:
        result = load_result_csv(f)
        if result:
            ttl = result.get('ttl_ms', 0)
            result['mode'] = f'Cache ({int(ttl)}ms)'
            data.append(result)
    
    if not data:
        print("No latency data found")
        return
    
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    
    # 图1: 平均延迟对比
    ax1 = axes[0]
    modes = [d['mode'] for d in data]
    avg_lats = [d['avg_latency_ns'] for d in data]
    
    colors = ['#e74c3c' if 'No Cache' in m else '#2ecc71' for m in modes]
    bars = ax1.bar(range(len(modes)), avg_lats, color=colors, edgecolor='black')
    
    ax1.set_ylabel('Average Latency (ns)', fontsize=11)
    ax1.set_title('Cache vs Shared Memory Latency', fontsize=12, fontweight='bold')
    ax1.set_xticks(range(len(modes)))
    ax1.set_xticklabels(modes, rotation=15, ha='right')
    ax1.grid(axis='y', alpha=0.3)
    
    for bar, val in zip(bars, avg_lats):
        height = bar.get_height()
        ax1.annotate(f'{val:.0f}',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3),
                    textcoords="offset points",
                    ha='center', va='bottom', fontsize=9)
    
    # 图2: P50 vs P99
    ax2 = axes[1]
    x = np.arange(len(modes))
    width = 0.35
    
    p50_vals = [d.get('p50_latency_ns', 0) for d in data]
    p99_vals = [d.get('p99_latency_ns', 0) for d in data]
    
    bars1 = ax2.bar(x - width/2, p50_vals, width, label='P50', color='#3498db', edgecolor='black')
    bars2 = ax2.bar(x + width/2, p99_vals, width, label='P99', color='#e74c3c', edgecolor='black')
    
    ax2.set_ylabel('Latency (ns)', fontsize=11)
    ax2.set_title('Latency Distribution', fontsize=12, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels(modes, rotation=15, ha='right')
    ax2.legend()
    ax2.grid(axis='y', alpha=0.3)
    
    plt.tight_layout()
    output = os.path.join(results_dir, 'exp4_latency.png')
    plt.savefig(output, dpi=150)
    plt.close()
    print(f"Saved: {output}")
    
    # 计算加速比
    if len(data) > 1:
        nocache_data = [d for d in data if 'No Cache' in d['mode']]
        if nocache_data:
            nocache_lat = nocache_data[0]['avg_latency_ns']
            print("\nSpeedup Analysis:")
            print("-" * 40)
            for d in data:
                if 'No Cache' not in d['mode']:
                    speedup = nocache_lat / d['avg_latency_ns']
                    print(f"  {d['mode']}: {speedup:.2f}x")

def plot_adaptive_ttl(results_dir):
    """绘制自适应TTL效果图表"""
    fixed_file = os.path.join(results_dir, 'exp4_adaptive_fixed.csv')
    adaptive_file = os.path.join(results_dir, 'exp4_adaptive_enabled.csv')
    
    if not os.path.exists(fixed_file) or not os.path.exists(adaptive_file):
        print("No adaptive TTL results found")
        return
    
    fixed = load_result_csv(fixed_file)
    adaptive = load_result_csv(adaptive_file)
    
    if not fixed or not adaptive:
        print("Invalid adaptive TTL data")
        return
    
    fig, ax = plt.subplots(figsize=(8, 6))
    
    modes = ['Fixed TTL', 'Adaptive TTL']
    hit_rates = [fixed.get('hit_rate', 0), adaptive.get('hit_rate', 0)]
    colors = ['#3498db', '#2ecc71']
    
    bars = ax.bar(modes, hit_rates, color=colors, edgecolor='black', width=0.5)
    
    for bar, val in zip(bars, hit_rates):
        height = bar.get_height()
        ax.annotate(f'{val:.2f}%',
                   xy=(bar.get_x() + bar.get_width() / 2, height),
                   xytext=(0, 3),
                   textcoords="offset points",
                   ha='center', va='bottom', fontsize=12, fontweight='bold')
    
    # 添加改进百分比
    if hit_rates[0] > 0:
        improvement = hit_rates[1] - hit_rates[0]
        color = 'green' if improvement > 0 else 'red'
        ax.annotate(f'{improvement:+.2f}%',
                   xy=(1, hit_rates[1]),
                   xytext=(1, hit_rates[1] + 2),
                   ha='center', fontsize=11, color=color, fontweight='bold')
    
    ax.set_ylabel('Hit Rate (%)', fontsize=12)
    ax.set_title('EXP-4: Adaptive TTL Effectiveness', fontsize=14, fontweight='bold')
    ax.set_ylim([95, 100.5])
    ax.grid(axis='y', alpha=0.3)
    
    plt.tight_layout()
    output = os.path.join(results_dir, 'exp4_adaptive.png')
    plt.savefig(output, dpi=150)
    plt.close()
    print(f"Saved: {output}")

def main():
    if len(sys.argv) < 2:
        results_dir = "results"
    else:
        results_dir = sys.argv[1]
    
    if not os.path.exists(results_dir):
        print(f"Results directory not found: {results_dir}")
        sys.exit(1)
    
    print("=" * 60)
    print("EXP-4: Cache Performance Visualization")
    print("=" * 60)
    print(f"Loading results from: {results_dir}")
    print()
    
    plot_hit_rate(results_dir)
    plot_latency_comparison(results_dir)
    plot_adaptive_ttl(results_dir)
    
    print()
    print("=" * 60)
    print("All plots generated!")
    print("=" * 60)

if __name__ == '__main__':
    main()
