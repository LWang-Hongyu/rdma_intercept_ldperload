#!/usr/bin/env python3
"""
论文图表生成工具
用于从实验数据生成论文所需的图表
"""

import matplotlib.pyplot as plt
import numpy as np
import json
import os
from pathlib import Path

# 设置中文字体支持
plt.rcParams['font.sans-serif'] = ['DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

RESULTS_DIR = Path("/home/why/rdma_intercept_ldpreload/paper_results")
FIGURES_DIR = Path("/home/why/rdma_intercept_ldpreload/paper_figures")
FIGURES_DIR.mkdir(exist_ok=True)

def parse_result_file(filepath):
    """解析实验结果文件"""
    data = {}
    if not os.path.exists(filepath):
        return data
    with open(filepath, 'r') as f:
        for line in f:
            if ':' in line:
                key, value = line.strip().split(':', 1)
                try:
                    data[key.strip()] = float(value.strip().split()[0])
                except:
                    data[key.strip()] = value.strip()
    return data

def generate_latency_comparison():
    """生成延迟对比图 (Figure 4)"""
    baseline = parse_result_file(RESULTS_DIR / "latency" / "baseline.txt")
    intercept = parse_result_file(RESULTS_DIR / "latency" / "with_intercept.txt")
    
    operations = ['QP Create', 'MR Reg']
    baseline_lat = [
        baseline.get('QP_CREATE_LATENCY', 0),
        baseline.get('MR_REG_LATENCY', 0)
    ]
    intercept_lat = [
        intercept.get('QP_CREATE_LATENCY', 0),
        intercept.get('MR_REG_LATENCY', 0)
    ]
    
    x = np.arange(len(operations))
    width = 0.35
    
    fig, ax = plt.subplots(figsize=(8, 6))
    rects1 = ax.bar(x - width/2, baseline_lat, width, label='Baseline', color='#2ecc71')
    rects2 = ax.bar(x + width/2, intercept_lat, width, label='With Intercept', color='#e74c3c')
    
    ax.set_ylabel('Latency (μs)', fontsize=12)
    ax.set_title('RDMA Operation Latency Comparison', fontsize=14, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels(operations)
    ax.legend()
    ax.grid(axis='y', alpha=0.3)
    
    # 添加数值标签
    for rect in rects1 + rects2:
        height = rect.get_height()
        ax.annotate(f'{height:.2f}',
                    xy=(rect.get_x() + rect.get_width() / 2, height),
                    xytext=(0, 3),
                    textcoords="offset points",
                    ha='center', va='bottom', fontsize=10)
    
    plt.tight_layout()
    plt.savefig(FIGURES_DIR / 'figure4_latency_comparison.png', dpi=300)
    print(f"✓ 生成图表: {FIGURES_DIR / 'figure4_latency_comparison.png'}")
    plt.close()
    
    # 计算并输出开销
    qp_overhead = (intercept_lat[0] - baseline_lat[0]) / baseline_lat[0] * 100 if baseline_lat[0] > 0 else 0
    mr_overhead = (intercept_lat[1] - baseline_lat[1]) / baseline_lat[1] * 100 if baseline_lat[1] > 0 else 0
    print(f"  QP创建开销: {qp_overhead:.1f}%")
    print(f"  MR注册开销: {mr_overhead:.1f}%")

def generate_scalability():
    """生成可扩展性图 (Figure 6)"""
    tenant_counts = [1, 5, 10]
    throughputs = []
    
    for count in tenant_counts:
        data = parse_result_file(RESULTS_DIR / "scalability" / f"tenant_{count}.txt")
        throughput = data.get('THROUGHPUT', 0)
        throughputs.append(throughput)
    
    fig, ax = plt.subplots(figsize=(8, 6))
    
    ax.plot(tenant_counts, throughputs, marker='o', linewidth=2, 
            markersize=10, color='#3498db')
    
    ax.set_xlabel('Number of Tenants', fontsize=12)
    ax.set_ylabel('Throughput (QPs/sec)', fontsize=12)
    ax.set_title('System Scalability', fontsize=14, fontweight='bold')
    ax.grid(True, alpha=0.3)
    
    # 添加数值标签
    for i, (x, y) in enumerate(zip(tenant_counts, throughputs)):
        ax.annotate(f'{y:.1f}',
                    xy=(x, y),
                    xytext=(0, 10),
                    textcoords="offset points",
                    ha='center', va='bottom', fontsize=10)
    
    plt.tight_layout()
    plt.savefig(FIGURES_DIR / 'figure6_scalability.png', dpi=300)
    print(f"✓ 生成图表: {FIGURES_DIR / 'figure6_scalability.png'}")
    plt.close()

def main():
    print("=" * 50)
    print("   论文图表生成工具")
    print("=" * 50)
    
    # 检查实验数据是否存在
    if not RESULTS_DIR.exists():
        print(f"错误: 未找到实验数据目录 {RESULTS_DIR}")
        print("请先运行实验套件: bash tests/benchmark_suite.sh")
        return 1
    
    print("\n生成论文图表...")
    print()
    
    try:
        generate_latency_comparison()
        generate_scalability()
        
        print()
        print("=" * 50)
        print(f"   所有图表已生成到: {FIGURES_DIR}")
        print("=" * 50)
        
        return 0
        
    except Exception as e:
        print(f"错误: {e}")
        import traceback
        traceback.print_exc()
        return 1

if __name__ == "__main__":
    exit(main())
