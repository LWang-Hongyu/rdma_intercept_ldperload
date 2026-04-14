#!/usr/bin/env python3
"""
EXP-7: 带宽隔离验证 - 绘图脚本
"""

import os
import sys
import matplotlib.pyplot as plt
import numpy as np

def load_summary(results_dir):
    """加载汇总文件"""
    summary_file = os.path.join(results_dir, 'exp7_summary.txt')
    
    if not os.path.exists(summary_file):
        return None
    
    data = {}
    with open(summary_file, 'r') as f:
        for line in f:
            if ':' in line and not line.startswith('='):
                key, val = line.split(':', 1)
                key = key.strip()
                val = val.strip()
                try:
                    data[key] = float(val.split()[0])
                except:
                    data[key] = val
    
    return data

def plot_isolation_results(results_dir):
    """绘制隔离结果图"""
    summary = load_summary(results_dir)
    
    if not summary:
        print("汇总文件不存在或格式错误")
        return
    
    baseline = summary.get('Baseline BW', 0)
    interference = summary.get('Interference BW', 0)
    isolation = summary.get('Isolation', 0)
    impact = summary.get('Impact', 0)
    
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    
    # 左图: 带宽对比
    ax1 = axes[0]
    categories = ['Baseline\n(Victim Only)', 'Interference\n(Victim+Attacker)']
    values = [baseline, interference]
    colors = ['#2ecc71', '#e74c3c']
    
    bars = ax1.bar(categories, values, color=colors, edgecolor='black', width=0.5)
    
    # 添加数值标签
    for bar, val in zip(bars, values):
        height = bar.get_height()
        ax1.annotate(f'{val:.1f} Gbps',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3),
                    textcoords="offset points",
                    ha='center', va='bottom', fontsize=12, fontweight='bold')
    
    ax1.set_ylabel('Bandwidth (Gbps)', fontsize=11)
    ax1.set_title('EXP-7: Bandwidth Isolation Test', fontsize=12, fontweight='bold')
    ax1.set_ylim([0, max(values) * 1.2])
    ax1.grid(axis='y', alpha=0.3)
    
    # 右图: 隔离度仪表盘
    ax2 = axes[1]
    
    # 绘制仪表盘
    theta = np.linspace(0, np.pi, 100)
    r = 1.0
    
    # 背景弧
    ax2.fill_between(np.cos(theta), np.sin(theta), 0, alpha=0.1, color='gray')
    
    # 颜色区域
    ax2.fill_between(np.cos(theta[:33]), np.sin(theta[:33]), 0, alpha=0.3, color='red', label='<90%')
    ax2.fill_between(np.cos(theta[33:66]), np.sin(theta[33:66]), 0, alpha=0.3, color='orange', label='90-95%')
    ax2.fill_between(np.cos(theta[66:]), np.sin(theta[66:]), 0, alpha=0.3, color='green', label='≥95%')
    
    # 指针
    isolation_rad = np.pi * (1 - isolation / 100)
    ax2.arrow(0, 0, 0.8 * np.cos(isolation_rad), 0.8 * np.sin(isolation_rad),
             head_width=0.05, head_length=0.05, fc='black', ec='black', linewidth=2)
    
    # 中心文字
    ax2.text(0, -0.3, f'{isolation:.1f}%', ha='center', va='center', 
            fontsize=24, fontweight='bold')
    ax2.text(0, -0.5, 'Isolation', ha='center', va='center', fontsize=12)
    
    # 添加刻度和标签
    for pct in [0, 50, 90, 95, 100]:
        rad = np.pi * (1 - pct / 100)
        ax2.plot([0.85 * np.cos(rad), 0.95 * np.cos(rad)],
                [0.85 * np.sin(rad), 0.95 * np.sin(rad)], 'k-', linewidth=1)
        ax2.text(1.1 * np.cos(rad), 1.1 * np.sin(rad), f'{pct}%',
                ha='center', va='center', fontsize=9)
    
    ax2.set_xlim([-1.3, 1.3])
    ax2.set_ylim([-0.7, 1.3])
    ax2.set_aspect('equal')
    ax2.axis('off')
    ax2.set_title(f'Isolation Degree (Target: ≥95%)\nImpact: {impact:.1f}%', 
                 fontsize=12, fontweight='bold')
    ax2.legend(loc='lower right')
    
    plt.tight_layout()
    output = os.path.join(results_dir, 'exp7_bandwidth_isolation.png')
    plt.savefig(output, dpi=150)
    plt.close()
    print(f"Saved: {output}")
    
    # 打印结果
    print("\nIsolation Test Results:")
    print("-" * 50)
    print(f"Baseline BW:      {baseline:.2f} Gbps")
    print(f"Interference BW:  {interference:.2f} Gbps")
    print(f"Isolation:        {isolation:.2f}%")
    print(f"Performance Drop: {impact:.2f}%")
    print("-" * 50)
    if isolation >= 95:
        print("Status: PASS ✓ (Isolation ≥ 95%)")
    elif isolation >= 90:
        print("Status: WARNING ⚠ (Isolation 90-95%)")
    else:
        print("Status: FAIL ✗ (Isolation < 90%)")

def main():
    if len(sys.argv) < 2:
        results_dir = "results"
    else:
        results_dir = sys.argv[1]
    
    if not os.path.exists(results_dir):
        print(f"Results directory not found: {results_dir}")
        sys.exit(1)
    
    print("=" * 60)
    print("EXP-7: Bandwidth Isolation Visualization")
    print("=" * 60)
    print(f"Loading results from: {results_dir}")
    print()
    
    plot_isolation_results(results_dir)
    
    print()
    print("=" * 60)
    print("Done!")
    print("=" * 60)

if __name__ == '__main__':
    main()
