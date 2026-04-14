#!/usr/bin/env python3
"""
EXP-6: RDMA数据面带宽影响测试绘图脚本
"""

import matplotlib.pyplot as plt
import numpy as np
import csv
import os

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'SimHei', 'Arial Unicode MS']
plt.rcParams['axes.unicode_minus'] = False

# 读取数据
def read_csv(filename):
    data = []
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            data.append({
                'msg_size': int(row['msg_size']),
                'bw_gbps': float(row['bw_gbps']),
                'msg_rate': float(row['msg_rate'])
            })
    return data

# 加载数据
results_dir = os.path.dirname(os.path.abspath(__file__)) + '/../results'
baseline_data = read_csv(f'{results_dir}/exp6_baseline.csv')
intercept_data = read_csv(f'{results_dir}/exp6_intercept.csv')

# 提取数据
msg_sizes = [d['msg_size'] for d in baseline_data]
baseline_bw = [d['bw_gbps'] for d in baseline_data]
intercept_bw = [d['bw_gbps'] for d in intercept_data]

# 计算性能差异
performance_diff = []
for base, inter in zip(baseline_bw, intercept_bw):
    diff = ((inter - base) / base) * 100 if base > 0 else 0
    performance_diff.append(diff)

# 创建图表
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

# 图1: 带宽对比
x = np.arange(len(msg_sizes))
width = 0.35

bars1 = ax1.bar(x - width/2, baseline_bw, width, label='Baseline (No LD_PRELOAD)', color='#3498db', edgecolor='black')
bars2 = ax1.bar(x + width/2, intercept_bw, width, label='With LD_PRELOAD Intercept', color='#e74c3c', edgecolor='black', alpha=0.8)

ax1.set_xlabel('Message Size', fontsize=12, fontweight='bold')
ax1.set_ylabel('Bandwidth (Gbps)', fontsize=12, fontweight='bold')
ax1.set_title('EXP-6: RDMA Bandwidth Impact Test\nData Plane Performance Comparison', fontsize=13, fontweight='bold')
ax1.set_xticks(x)
ax1.set_xticklabels(['64KB', '256KB', '1MB'], fontsize=11)
ax1.legend(loc='upper right', fontsize=10)
ax1.grid(axis='y', alpha=0.3, linestyle='--')
ax1.set_ylim(85, 95)

# 在柱状图上添加数值标签
for bar in bars1:
    height = bar.get_height()
    ax1.annotate(f'{height:.2f}',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3),
                textcoords="offset points",
                ha='center', va='bottom', fontsize=9, fontweight='bold')

for bar in bars2:
    height = bar.get_height()
    ax1.annotate(f'{height:.2f}',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3),
                textcoords="offset points",
                ha='center', va='bottom', fontsize=9, fontweight='bold')

# 图2: 性能差异百分比
colors = ['#27ae60' if d >= 0 else '#e74c3c' for d in performance_diff]
bars3 = ax2.bar(x, performance_diff, width=0.5, color=colors, edgecolor='black', alpha=0.8)

ax2.set_xlabel('Message Size', fontsize=12, fontweight='bold')
ax2.set_ylabel('Performance Difference (%)', fontsize=12, fontweight='bold')
ax2.set_title('Performance Impact of LD_PRELOAD\n(Positive = Better with Intercept)', fontsize=13, fontweight='bold')
ax2.set_xticks(x)
ax2.set_xticklabels(['64KB', '256KB', '1MB'], fontsize=11)
ax2.axhline(y=0, color='black', linestyle='-', linewidth=1)
ax2.axhline(y=5, color='orange', linestyle='--', linewidth=1.5, alpha=0.7, label='±5% threshold')
ax2.axhline(y=-5, color='orange', linestyle='--', linewidth=1.5, alpha=0.7)
ax2.grid(axis='y', alpha=0.3, linestyle='--')
ax2.legend(loc='upper right', fontsize=10)

# 在柱状图上添加数值标签
for bar in bars3:
    height = bar.get_height()
    ax2.annotate(f'{height:+.2f}%',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3 if height >= 0 else -12),
                textcoords="offset points",
                ha='center', va='bottom' if height >= 0 else 'top', fontsize=10, fontweight='bold')

plt.tight_layout()

# 保存图表
output_dir = os.path.dirname(os.path.abspath(__file__))
plt.savefig(f'{output_dir}/exp6_bandwidth_impact.png', dpi=150, bbox_inches='tight')
print(f"图表已保存: {output_dir}/exp6_bandwidth_impact.png")

# 打印结果摘要
print("\n" + "="*60)
print("EXP-6: RDMA数据面带宽影响测试 - 结果摘要")
print("="*60)
print(f"{'Message Size':<15} {'Baseline':<12} {'Intercept':<12} {'Diff':<12}")
print("-"*60)
for i, size in enumerate(msg_sizes):
    size_str = {65536: '64KB', 262144: '256KB', 1048576: '1MB'}.get(size, f'{size}B')
    print(f"{size_str:<15} {baseline_bw[i]:<12.2f} {intercept_bw[i]:<12.2f} {performance_diff[i]:>+10.2f}%")

print("-"*60)
avg_diff = np.mean(performance_diff)
print(f"{'Average':<15} {'':<12} {'':<12} {avg_diff:>+10.2f}%")
print("="*60)

if abs(avg_diff) < 5:
    print("\n结论: LD_PRELOAD对数据面性能影响 < 5%，符合预期")
    print("      控制面拦截不干扰数据面传输性能")
else:
    print(f"\n结论: LD_PRELOAD对数据面性能影响为 {avg_diff:+.2f}%")

plt.close()
