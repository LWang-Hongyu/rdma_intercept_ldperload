#!/usr/bin/env python3
"""
EXP-1: 微基准测试可视化 - 从实际结果文件读取数据
"""

import matplotlib.pyplot as plt
import numpy as np
import re

def parse_results(filename):
    """解析实验结果文件"""
    results = {}
    current_section = None
    
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if line.startswith('## QP_CREATE_LATENCY'):
                current_section = 'qp_create'
                results[current_section] = {}
            elif line.startswith('## QP_DESTROY_LATENCY'):
                current_section = 'qp_destroy'
                results[current_section] = {}
            elif line.startswith('## MR_REG_LATENCY'):
                current_section = 'mr_reg'
                results[current_section] = {}
            elif current_section and ':' in line:
                key, value = line.split(':', 1)
                key = key.strip()
                value = float(value.strip())
                results[current_section][key] = value
    
    return results

# 读取实际结果
baseline = parse_results('results/baseline.csv')
intercept = parse_results('results/with_intercept.csv')

print("基线数据:", baseline)
print("拦截数据:", intercept)

# 创建图表
fig, axes = plt.subplots(2, 2, figsize=(14, 10))

# ===== 图1: 延迟对比柱状图 =====
ax1 = axes[0, 0]

categories = ['QP Create', 'MR Register']
baseline_means = [baseline['qp_create']['MEAN'], baseline['mr_reg']['MEAN']]
intercept_means = [intercept['qp_create']['MEAN'], intercept['mr_reg']['MEAN']]

x = np.arange(len(categories))
width = 0.35

bars1 = ax1.bar(x - width/2, baseline_means, width, label='Baseline (No Intercept)', 
                color='#3498db', alpha=0.8, edgecolor='black')
bars2 = ax1.bar(x + width/2, intercept_means, width, label='With Intercept', 
                color='#e74c3c', alpha=0.8, edgecolor='black')

# 添加数值标签
for bar in bars1:
    height = bar.get_height()
    ax1.annotate(f'{height:.1f}μs',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3), textcoords="offset points",
                ha='center', va='bottom', fontsize=10)

for bar in bars2:
    height = bar.get_height()
    ax1.annotate(f'{height:.1f}μs',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3), textcoords="offset points",
                ha='center', va='bottom', fontsize=10)

# 计算并显示开销百分比
qp_overhead = (intercept_means[0] - baseline_means[0]) / baseline_means[0] * 100
mr_overhead = (intercept_means[1] - baseline_means[1]) / baseline_means[1] * 100

ax1.annotate(f'{qp_overhead:+.1f}%', xy=(0, max(baseline_means[0], intercept_means[0]) + 20),
            fontsize=11, ha='center', color='green' if abs(qp_overhead) < 20 else 'red', fontweight='bold')
ax1.annotate(f'{mr_overhead:+.1f}%', xy=(1, max(baseline_means[1], intercept_means[1]) + 2),
            fontsize=11, ha='center', color='green' if abs(mr_overhead) < 20 else 'red', fontweight='bold')

ax1.set_ylabel('Latency (microseconds)', fontsize=12)
ax1.set_title('EXP-1: Overhead Comparison (Real Data)', fontsize=14, fontweight='bold')
ax1.set_xticks(x)
ax1.set_xticklabels(categories)
ax1.legend()
ax1.grid(axis='y', alpha=0.3)

# ===== 图2: QP创建延迟分布 =====
ax2 = axes[0, 1]

metrics = ['MEAN', 'P50', 'P95', 'P99']
baseline_qp_dist = [baseline['qp_create'][m] for m in metrics]
intercept_qp_dist = [intercept['qp_create'][m] for m in metrics]

x2 = np.arange(len(metrics))
width2 = 0.35

bars3 = ax2.bar(x2 - width2/2, baseline_qp_dist, width2, label='Baseline', 
                color='#3498db', alpha=0.8, edgecolor='black')
bars4 = ax2.bar(x2 + width2/2, intercept_qp_dist, width2, label='With Intercept', 
                color='#e74c3c', alpha=0.8, edgecolor='black')

ax2.set_ylabel('Latency (microseconds)', fontsize=12)
ax2.set_title('EXP-1: QP Creation Latency Distribution', fontsize=14, fontweight='bold')
ax2.set_xticks(x2)
ax2.set_xticklabels(metrics)
ax2.legend()
ax2.grid(axis='y', alpha=0.3)

# ===== 图3: MR注册延迟分布 =====
ax3 = axes[1, 0]

baseline_mr_dist = [baseline['mr_reg'][m] for m in metrics if m in baseline['mr_reg']]
intercept_mr_dist = [intercept['mr_reg'][m] for m in metrics if m in intercept['mr_reg']]

x3 = np.arange(len(baseline_mr_dist))
width3 = 0.35

bars5 = ax3.bar(x3 - width3/2, baseline_mr_dist, width3, label='Baseline', 
                color='#3498db', alpha=0.8, edgecolor='black')
bars6 = ax3.bar(x3 + width3/2, intercept_mr_dist, width3, label='With Intercept', 
                color='#e74c3c', alpha=0.8, edgecolor='black')

ax3.set_ylabel('Latency (microseconds)', fontsize=12)
ax3.set_title('EXP-1: MR Registration Latency Distribution', fontsize=14, fontweight='bold')
ax3.set_xticks(x3)
ax3.set_xticklabels([m for m in metrics if m in baseline['mr_reg']])
ax3.legend()
ax3.grid(axis='y', alpha=0.3)

# ===== 图4: 详细统计表格 =====
ax4 = axes[1, 1]
ax4.axis('off')

# 准备表格数据
table_data = [
    ['Metric', 'Baseline', 'Intercept', 'Overhead'],
    ['QP MEAN', f"{baseline['qp_create']['MEAN']:.1f}μs", f"{intercept['qp_create']['MEAN']:.1f}μs", f"{qp_overhead:+.1f}%"],
    ['QP P50', f"{baseline['qp_create']['P50']:.1f}μs", f"{intercept['qp_create']['P50']:.1f}μs", 
     f"{(intercept['qp_create']['P50']-baseline['qp_create']['P50'])/baseline['qp_create']['P50']*100:+.1f}%"],
    ['QP P95', f"{baseline['qp_create']['P95']:.1f}μs", f"{intercept['qp_create']['P95']:.1f}μs",
     f"{(intercept['qp_create']['P95']-baseline['qp_create']['P95'])/baseline['qp_create']['P95']*100:+.1f}%"],
    ['QP P99', f"{baseline['qp_create']['P99']:.1f}μs", f"{intercept['qp_create']['P99']:.1f}μs",
     f"{(intercept['qp_create']['P99']-baseline['qp_create']['P99'])/baseline['qp_create']['P99']*100:+.1f}%"],
    ['MR MEAN', f"{baseline['mr_reg']['MEAN']:.1f}μs", f"{intercept['mr_reg']['MEAN']:.1f}μs", f"{mr_overhead:+.1f}%"],
]

table = ax4.table(cellText=table_data[1:], colLabels=table_data[0],
                  cellLoc='center', loc='center',
                  colColours=['#4472C4']*4)
table.auto_set_font_size(False)
table.set_fontsize(10)
table.scale(1.2, 1.8)

# 设置表头颜色
for i in range(4):
    table[(0, i)].set_text_props(color='white', fontweight='bold')

ax4.set_title('EXP-1: Detailed Statistics (Real Data from 10.157.195.92)', 
              fontsize=14, fontweight='bold', pad=20)

plt.tight_layout()
plt.savefig('figures/exp1_real_data.png', dpi=300, bbox_inches='tight')
print("✓ EXP-1图表已保存: figures/exp1_real_data.png")

print("\n" + "="*60)
print("EXP-1: 微基准测试 - 完成! (使用实际数据)")
print("="*60)
print(f"\nQP创建延迟:")
print(f"  基线: {baseline['qp_create']['MEAN']:.1f}μs")
print(f"  拦截: {intercept['qp_create']['MEAN']:.1f}μs")
print(f"  开销: {qp_overhead:+.1f}%")

print(f"\nMR注册延迟:")
print(f"  基线: {baseline['mr_reg']['MEAN']:.1f}μs")
print(f"  拦截: {intercept['mr_reg']['MEAN']:.1f}μs")
print(f"  开销: {mr_overhead:+.1f}%")

if abs(qp_overhead) < 20 and abs(mr_overhead) < 20:
    print("\n✓ 拦截开销 < 20%，满足设计要求！")
else:
    print("\n✗ 拦截开销 >= 20%，需要优化")
