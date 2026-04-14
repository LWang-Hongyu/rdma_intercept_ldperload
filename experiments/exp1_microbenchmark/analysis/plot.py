#!/usr/bin/env python3
"""
EXP-1: 微基准测试可视化 v2 (排除冷启动)
"""

import matplotlib.pyplot as plt
import numpy as np

# 直接定义数据（从结果文件手动提取）
# Baseline (无拦截)
baseline_qp = {'MEAN': 464.331, 'P50': 457.893, 'P95': 496.879, 'P99': 729.635}
baseline_mr = {'MEAN': 22.307, 'P50': 21.679, 'P95': 27.038, 'P99': 30.172}

# Intercepted (有拦截)
intercepted_qp = {'MEAN': 486.8, 'P50': 481.5, 'P95': 518.2, 'P99': 910.2}
intercepted_mr = {'MEAN': 28.9, 'P50': 28.6, 'P95': 30.7}

# 冷启动数据
cold_start_1st = 26262.9
cold_start_10th = 572.7

# 创建图表
fig, axes = plt.subplots(2, 2, figsize=(14, 10))

# ===== 图1: 延迟对比柱状图 =====
ax1 = axes[0, 0]

categories = ['QP Create\n(Normal)', 'MR Register']
baseline_means = [baseline_qp['MEAN'], baseline_mr['MEAN']]
intercepted_means = [intercepted_qp['MEAN'], intercepted_mr['MEAN']]

x = np.arange(len(categories))
width = 0.35

bars1 = ax1.bar(x - width/2, baseline_means, width, label='Baseline (No Intercept)', 
                color='#3498db', alpha=0.8, edgecolor='black')
bars2 = ax1.bar(x + width/2, intercepted_means, width, label='With Intercept', 
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
qp_overhead = (intercepted_qp['MEAN'] - baseline_qp['MEAN']) / baseline_qp['MEAN'] * 100
mr_overhead = (intercepted_mr['MEAN'] - baseline_mr['MEAN']) / baseline_mr['MEAN'] * 100

ax1.annotate(f'+{qp_overhead:.1f}%', xy=(0, max(baseline_means[0], intercepted_means[0]) + 15),
            fontsize=11, ha='center', color='red', fontweight='bold')
ax1.annotate(f'+{mr_overhead:.1f}%', xy=(1, max(baseline_means[1], intercepted_means[1]) + 3),
            fontsize=11, ha='center', color='red', fontweight='bold')

ax1.set_ylabel('Latency (microseconds)', fontsize=12)
ax1.set_title('EXP-1: Overhead Comparison (Normal Operations)', fontsize=14, fontweight='bold')
ax1.set_xticks(x)
ax1.set_xticklabels(categories)
ax1.legend()
ax1.grid(axis='y', alpha=0.3)

# ===== 图2: 冷启动vs正常创建 =====
ax2 = axes[0, 1]

stages = ['1st QP\n(Cold Start)', '10th QP\n(Warm)', 'Normal QP\n(Avg)']
latencies = [cold_start_1st/1000, cold_start_10th/1000, baseline_qp['MEAN']/1000]
colors = ['#e67e22', '#f1c40f', '#2ecc71']

bars = ax2.bar(stages, latencies, color=colors, alpha=0.8, edgecolor='black')

for bar, lat in zip(bars, latencies):
    height = bar.get_height()
    ax2.annotate(f'{lat:.2f}ms',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3), textcoords="offset points",
                ha='center', va='bottom', fontsize=11, fontweight='bold')

ax2.set_ylabel('Latency (milliseconds)', fontsize=12)
ax2.set_title('EXP-1: Cold Start vs Normal Creation', fontsize=14, fontweight='bold')
ax2.grid(axis='y', alpha=0.3)

# 添加说明文字
ax2.text(0.5, 0.95, 
        f'Cold Start Overhead: {cold_start_1st/cold_start_10th:.0f}x\n'
        f'(First QP takes {cold_start_1st/1000:.1f}ms vs {cold_start_10th:.0f}μs)',
        transform=ax2.transAxes, fontsize=10, verticalalignment='top', horizontalalignment='center',
        bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))

# ===== 图3: QP创建延迟分布 =====
ax3 = axes[1, 0]

qp_metrics = ['MEAN', 'P50', 'P95', 'P99']
baseline_vals = [baseline_qp[m] for m in qp_metrics]
intercepted_vals = [intercepted_qp[m] for m in qp_metrics]

x = np.arange(len(qp_metrics))
width = 0.35

ax3.bar(x - width/2, baseline_vals, width, label='Baseline', color='#3498db', alpha=0.8)
ax3.bar(x + width/2, intercepted_vals, width, label='With Intercept', color='#e74c3c', alpha=0.8)

ax3.set_ylabel('Latency (microseconds)', fontsize=12)
ax3.set_title('EXP-1: QP Creation Latency Distribution', fontsize=14, fontweight='bold')
ax3.set_xticks(x)
ax3.set_xticklabels(qp_metrics)
ax3.legend()
ax3.grid(axis='y', alpha=0.3)

# ===== 图4: 详细数据表格 =====
ax4 = axes[1, 1]
ax4.axis('off')

# 创建表格数据
table_data = [
    ['Metric', 'Baseline', 'Intercepted', 'Overhead'],
    ['QP MEAN', f'{baseline_qp["MEAN"]:.0f}μs', f'{intercepted_qp["MEAN"]:.0f}μs', f'+{qp_overhead:.1f}%'],
    ['QP P50', f'{baseline_qp["P50"]:.0f}μs', f'{intercepted_qp["P50"]:.0f}μs', '+5.2%'],
    ['QP P95', f'{baseline_qp["P95"]:.0f}μs', f'{intercepted_qp["P95"]:.0f}μs', '+4.3%'],
    ['MR MEAN', f'{baseline_mr["MEAN"]:.1f}μs', f'{intercepted_mr["MEAN"]:.1f}μs', f'+{mr_overhead:.1f}%'],
    ['Cold Start', '26.3ms', '26.2ms', '~0%'],
]

# 绘制表格
table = ax4.table(cellText=table_data[1:], colLabels=table_data[0],
                 cellLoc='center', loc='center',
                 colWidths=[0.25, 0.25, 0.25, 0.25])
table.auto_set_font_size(False)
table.set_fontsize(10)
table.scale(1.2, 1.8)

# 设置表头样式
for i in range(4):
    table[(0, i)].set_facecolor('#3498db')
    table[(0, i)].set_text_props(weight='bold', color='white')

ax4.set_title('EXP-1: Detailed Statistics', fontsize=14, fontweight='bold', pad=20)

plt.suptitle('EXP-1: Microbenchmark Results v2 (Cold Start Excluded)', 
             fontsize=16, fontweight='bold', y=0.98)
plt.tight_layout()
plt.savefig('figures/exp1_microbenchmark_v2.png', dpi=300, bbox_inches='tight')
plt.savefig('figures/exp1_microbenchmark_v2.pdf', bbox_inches='tight')
print("✓ EXP-1 v2图表已保存: figures/exp1_microbenchmark_v2.png")
plt.close()

print("\n" + "="*60)
print("EXP-1 v2: 微基准测试 - 完成!")
print("="*60)
print(f"\n冷启动效应:")
print(f"  第1个QP: {cold_start_1st/1000:.1f} ms (冷启动)")
print(f"  第10个QP: {cold_start_10th:.0f} μs (热状态)")
print(f"\n正常操作开销:")
print(f"  QP创建: +{qp_overhead:.1f}% (~+{intercepted_qp['MEAN']-baseline_qp['MEAN']:.0f}μs)")
print(f"  MR注册: +{mr_overhead:.1f}% (~+{intercepted_mr['MEAN']-baseline_mr['MEAN']:.1f}μs)")
