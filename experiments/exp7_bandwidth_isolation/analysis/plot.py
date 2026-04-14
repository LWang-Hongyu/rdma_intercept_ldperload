#!/usr/bin/env python3
"""
EXP-7: 带宽隔离验证测试绘图脚本
"""

import matplotlib.pyplot as plt
import numpy as np
import os

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'SimHei', 'Arial Unicode MS']
plt.rcParams['axes.unicode_minus'] = False

# 读取数据
results_dir = os.path.dirname(os.path.abspath(__file__)) + '/../results'

with open(f'{results_dir}/exp7_baseline_bw.txt', 'r') as f:
    baseline_bw = float(f.read().strip())

with open(f'{results_dir}/exp7_interference_bw.txt', 'r') as f:
    interference_bw = float(f.read().strip())

# 计算隔离度
isolation_degree = (interference_bw / baseline_bw) * 100
performance_drop = ((baseline_bw - interference_bw) / baseline_bw) * 100

# 创建图表
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

# 图1: 带宽对比
scenarios = ['Baseline\n(Victim Only)', 'Interference\n(Victim + Attacker)']
bandwidths = [baseline_bw, interference_bw]
colors = ['#3498db', '#e74c3c']

bars1 = ax1.bar(scenarios, bandwidths, color=colors, edgecolor='black', width=0.5, alpha=0.8)

ax1.set_ylabel('Bandwidth (Gbps)', fontsize=12, fontweight='bold')
ax1.set_title('EXP-7: Bandwidth Isolation Test\nVictim Bandwidth Comparison', fontsize=13, fontweight='bold')
ax1.grid(axis='y', alpha=0.3, linestyle='--')
ax1.set_ylim(0, max(bandwidths) * 1.2)

# 在柱状图上添加数值标签
for bar in bars1:
    height = bar.get_height()
    ax1.annotate(f'{height:.2f} Gbps',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3),
                textcoords="offset points",
                ha='center', va='bottom', fontsize=11, fontweight='bold')

# 添加性能下降箭头
ax1.annotate('', xy=(1, interference_bw), xytext=(1, baseline_bw),
            arrowprops=dict(arrowstyle='->', color='red', lw=2))
ax1.text(1.15, (baseline_bw + interference_bw) / 2, f'-{performance_drop:.1f}%',
        fontsize=12, color='red', fontweight='bold', va='center')

# 图2: 隔离度仪表盘
ax2.set_xlim(0, 100)
ax2.set_ylim(0, 10)
ax2.axis('off')

# 绘制隔离度指示
center_x = 50
center_y = 5
radius = 3.5

# 绘制背景弧
theta_bg = np.linspace(0, np.pi, 100)
x_bg = center_x + radius * np.cos(theta_bg)
y_bg = center_y + radius * np.sin(theta_bg)
ax2.plot(x_bg, y_bg, 'k-', linewidth=3, alpha=0.3)

# 绘制隔离度弧
theta_iso = np.linspace(0, np.pi * isolation_degree / 100, 100)
x_iso = center_x + radius * np.cos(theta_iso)
y_iso = center_y + radius * np.sin(theta_iso)
color = '#27ae60' if isolation_degree >= 95 else ('#f39c12' if isolation_degree >= 80 else '#e74c3c')
ax2.plot(x_iso, y_iso, color=color, linewidth=8)

# 添加刻度
for i in range(0, 101, 25):
    theta = np.pi * i / 100
    x1 = center_x + (radius - 0.3) * np.cos(theta)
    y1 = center_y + (radius - 0.3) * np.sin(theta)
    x2 = center_x + (radius + 0.3) * np.cos(theta)
    y2 = center_y + (radius + 0.3) * np.sin(theta)
    ax2.plot([x1, x2], [y1, y2], 'k-', linewidth=2)
    x_text = center_x + (radius + 0.8) * np.cos(theta)
    y_text = center_y + (radius + 0.8) * np.sin(theta)
    ax2.text(x_text, y_text, f'{i}%', ha='center', va='center', fontsize=10)

# 添加指针
theta_ptr = np.pi * isolation_degree / 100
x_ptr = center_x + radius * np.cos(theta_ptr)
y_ptr = center_y + radius * np.sin(theta_ptr)
ax2.plot([center_x, x_ptr], [center_y, y_ptr], color=color, linewidth=4)
ax2.plot(center_x, center_y, 'ko', markersize=15)

# 添加数值显示
ax2.text(center_x, center_y - 1.5, f'{isolation_degree:.1f}%',
        ha='center', va='center', fontsize=28, fontweight='bold', color=color)
ax2.text(center_x, center_y - 2.5, 'Isolation Degree',
        ha='center', va='center', fontsize=12, color='gray')

# 添加状态文本
if isolation_degree >= 95:
    status_text = 'EXCELLENT'
    status_color = '#27ae60'
elif isolation_degree >= 80:
    status_text = 'GOOD'
    status_color = '#f39c12'
elif isolation_degree >= 50:
    status_text = 'FAIR'
    status_color = '#e67e22'
else:
    status_text = 'SHARED NETWORK'
    status_color = '#e74c3c'

ax2.text(center_x, center_y + 4.5, status_text,
        ha='center', va='center', fontsize=16, fontweight='bold', color=status_color)

ax2.set_title('Isolation Degree Measurement\n(Target: ≥95%)', fontsize=13, fontweight='bold')

plt.tight_layout()

# 保存图表
output_dir = os.path.dirname(os.path.abspath(__file__))
plt.savefig(f'{output_dir}/exp7_bandwidth_isolation.png', dpi=150, bbox_inches='tight')
print(f"图表已保存: {output_dir}/exp7_bandwidth_isolation.png")

# 打印结果摘要
print("\n" + "="*60)
print("EXP-7: 带宽隔离验证测试 - 结果摘要")
print("="*60)
print(f"基线带宽:     {baseline_bw:.2f} Gbps (Victim单独运行)")
print(f"干扰带宽:     {interference_bw:.2f} Gbps (Victim+Attacker同时运行)")
print(f"隔离度:       {isolation_degree:.2f}%")
print(f"性能下降:     {performance_drop:.2f}%")
print("-"*60)

if isolation_degree >= 95:
    print("状态: EXCELLENT ✓ (隔离度 ≥ 95%)")
    print("      实现了有效的带宽隔离")
elif isolation_degree >= 80:
    print("状态: GOOD (隔离度 80-95%)")
    print("      较好的带宽隔离效果")
elif isolation_degree >= 50:
    print("状态: FAIR (隔离度 50-80%)")
    print("      部分带宽隔离效果")
else:
    print("状态: SHARED NETWORK (隔离度 < 50%)")
    print("      在共享100Gbps链路上，带宽被多租户分摊")
    print("      这是共享网络的预期行为")
    print("      严格隔离需要硬件QoS或SR-IOV支持")

print("="*60)

plt.close()
