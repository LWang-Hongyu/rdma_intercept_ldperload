#!/usr/bin/env python3
"""
EXP-6: RDMA数据面带宽影响测试 - 绘图脚本
"""

import os
import sys
import csv
import matplotlib.pyplot as plt
import numpy as np

def load_csv(filepath):
    """加载CSV文件"""
    data = []
    with open(filepath, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                data.append({
                    'msg_size': int(row['msg_size']),
                    'iteration': int(row['iteration']),
                    'bw_gbps': float(row['bw_gbps']),
                    'msg_rate': float(row['msg_rate'])
                })
            except:
                pass
    return data

def aggregate_data(data):
    """按消息大小聚合数据"""
    sizes = sorted(set(d['msg_size'] for d in data))
    result = {}
    for size in sizes:
        size_data = [d['bw_gbps'] for d in data if d['msg_size'] == size and d['bw_gbps'] > 0]
        if size_data:
            result[size] = {
                'mean': np.mean(size_data),
                'std': np.std(size_data),
                'min': np.min(size_data),
                'max': np.max(size_data)
            }
    return result

def plot_bandwidth_comparison(results_dir):
    """绘制带宽对比图"""
    baseline_file = os.path.join(results_dir, 'exp6_baseline.csv')
    intercept_file = os.path.join(results_dir, 'exp6_intercept.csv')
    
    if not os.path.exists(baseline_file) or not os.path.exists(intercept_file):
        print("结果文件不存在")
        return
    
    baseline_data = aggregate_data(load_csv(baseline_file))
    intercept_data = aggregate_data(load_csv(intercept_file))
    
    if not baseline_data or not intercept_data:
        print("数据为空")
        return
    
    # 准备数据
    sizes = sorted(set(baseline_data.keys()) & set(intercept_data.keys()))
    
    baseline_bw = [baseline_data[s]['mean'] for s in sizes]
    intercept_bw = [intercept_data[s]['mean'] for s in sizes]
    
    baseline_err = [baseline_data[s]['std'] for s in sizes]
    intercept_err = [intercept_data[s]['std'] for s in sizes]
    
    # 计算影响百分比
    impact_pct = [(baseline_data[s]['mean'] - intercept_data[s]['mean']) / baseline_data[s]['mean'] * 100 
                  if baseline_data[s]['mean'] > 0 else 0 for s in sizes]
    
    # 创建图表
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    
    # 左图: 带宽对比
    ax1 = axes[0]
    x = np.arange(len(sizes))
    width = 0.35
    
    bars1 = ax1.bar(x - width/2, baseline_bw, width, label='Baseline (No Intercept)', 
                    color='#3498db', yerr=baseline_err, capsize=3)
    bars2 = ax1.bar(x + width/2, intercept_bw, width, label='With Intercept', 
                    color='#e74c3c', yerr=intercept_err, capsize=3)
    
    ax1.set_xlabel('Message Size (bytes)', fontsize=11)
    ax1.set_ylabel('Bandwidth (Gbps)', fontsize=11)
    ax1.set_title('EXP-6: Bandwidth Impact of LD_PRELOAD Interception', fontsize=12, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels([f'{s//1024}K' if s >= 1024 else str(s) for s in sizes], rotation=45)
    ax1.legend()
    ax1.grid(axis='y', alpha=0.3)
    
    # 右图: 影响百分比
    ax2 = axes[1]
    colors = ['green' if p < 5 else 'orange' if p < 10 else 'red' for p in impact_pct]
    bars = ax2.bar(x, impact_pct, color=colors, edgecolor='black')
    
    # 添加数值标签
    for bar, val in zip(bars, impact_pct):
        height = bar.get_height()
        ax2.annotate(f'{val:.1f}%',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3),
                    textcoords="offset points",
                    ha='center', va='bottom', fontsize=9)
    
    ax2.set_xlabel('Message Size (bytes)', fontsize=11)
    ax2.set_ylabel('Performance Impact (%)', fontsize=11)
    ax2.set_title('Bandwidth Impact Percentage', fontsize=12, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels([f'{s//1024}K' if s >= 1024 else str(s) for s in sizes], rotation=45)
    ax2.axhline(y=5, color='orange', linestyle='--', linewidth=1.5, alpha=0.7, label='5% threshold')
    ax2.axhline(y=10, color='red', linestyle='--', linewidth=1.5, alpha=0.7, label='10% threshold')
    ax2.legend()
    ax2.grid(axis='y', alpha=0.3)
    
    plt.tight_layout()
    output = os.path.join(results_dir, 'exp6_bandwidth_impact.png')
    plt.savefig(output, dpi=150)
    plt.close()
    print(f"Saved: {output}")
    
    # 打印汇总
    print("\nBandwidth Impact Summary:")
    print("-" * 60)
    print(f"{'Size':<12} {'Baseline':<12} {'Intercept':<12} {'Impact':<10}")
    print("-" * 60)
    for s in sizes:
        base = baseline_data[s]['mean']
        inter = intercept_data[s]['mean']
        impact = (base - inter) / base * 100 if base > 0 else 0
        size_str = f"{s//1024}KB" if s >= 1024 else f"{s}B"
        print(f"{size_str:<12} {base:<12.2f} {inter:<12.2f} {impact:<10.2f}%")
    print("-" * 60)

def main():
    if len(sys.argv) < 2:
        results_dir = "results"
    else:
        results_dir = sys.argv[1]
    
    if not os.path.exists(results_dir):
        print(f"Results directory not found: {results_dir}")
        sys.exit(1)
    
    print("=" * 60)
    print("EXP-6: Bandwidth Impact Visualization")
    print("=" * 60)
    print(f"Loading results from: {results_dir}")
    print()
    
    plot_bandwidth_comparison(results_dir)
    
    print()
    print("=" * 60)
    print("Done!")
    print("=" * 60)

if __name__ == '__main__':
    main()
