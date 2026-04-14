#!/usr/bin/env python3
"""
EXP-1: 微基准测试可视化 - 自动读取结果文件
"""

import matplotlib.pyplot as plt
import numpy as np
import re
import os

RESULTS_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'results')

def parse_result_file(filepath):
    """解析结果文件"""
    data = {}
    current_section = None
    
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            if line.startswith('##'):
                current_section = line[3:].strip().lower().replace(' ', '_')
                data[current_section] = {}
            elif ':' in line and current_section:
                key, value = line.split(':', 1)
                try:
                    data[current_section][key.strip().lower()] = float(value.strip())
                except:
                    pass
    
    return data

# 读取结果文件
baseline = parse_result_file(os.path.join(RESULTS_DIR, 'baseline.csv'))
intercepted = parse_result_file(os.path.join(RESULTS_DIR, 'with_intercept.csv'))

# 提取数据
baseline_qp = baseline.get('qp_create_latency_(us)', {})
baseline_mr = baseline.get('mr_reg_latency_(us)', {})
baseline_qpd = baseline.get('qp_destroy_latency_(us)', {})

intercepted_qp = intercepted.get('qp_create_latency_(us)', {})
intercepted_mr = intercepted.get('mr_reg_latency_(us)', {})
intercepted_qpd = intercepted.get('qp_destroy_latency_(us)', {})

# 创建输出目录
os.makedirs('figures', exist_ok=True)

# 创建图表
fig, axes = plt.subplots(2, 2, figsize=(14, 10))

# ===== 图1: 延迟对比柱状图 =====
ax1 = axes[0, 0]

categories = ['QP Create', 'QP Destroy', 'MR Register']
baseline_means = [baseline_qp.get('mean', 0), baseline_qpd.get('mean', 0), baseline_mr.get('mean', 0)]
intercepted_means = [intercepted_qp.get('mean', 0), intercepted_qpd.get('mean', 0), intercepted_mr.get('mean', 0)]

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
                ha='center', va='bottom', fontsize=9)

for bar in bars2:
    height = bar.get_height()
    ax1.annotate(f'{height:.1f}μs',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3), textcoords="offset points",
                ha='center', va='bottom', fontsize=9)

# 计算并显示开销百分比
for i, (b, inter) in enumerate(zip(baseline_means, intercepted_means)):
    if b > 0:
        overhead = (inter - b) / b * 100
        sign = '+' if overhead >= 0 else ''
        ax1.annotate(f'{sign}{overhead:.1f}%', 
                    xy=(i, max(b, inter) * 1.1),
                    fontsize=10, ha='center', 
                    color='green' if overhead < 5 else ('orange' if overhead < 10 else 'red'),
                    fontweight='bold')

ax1.set_ylabel('Latency (microseconds)', fontsize=12)
ax1.set_title('EXP-1: Latency Comparison', fontsize=14, fontweight='bold')
ax1.set_xticks(x)
ax1.set_xticklabels(categories)
ax1.legend()
ax1.grid(axis='y', alpha=0.3)

# ===== 图2: QP创建延迟分布 =====
ax2 = axes[0, 1]

qp_metrics = ['MEAN', 'P50', 'P95', 'P99']
baseline_vals = [baseline_qp.get('mean', 0), baseline_qp.get('p50', 0), 
                 baseline_qp.get('p95', 0), baseline_qp.get('p99', 0)]
intercepted_vals = [intercepted_qp.get('mean', 0), intercepted_qp.get('p50', 0), 
                    intercepted_qp.get('p95', 0), intercepted_qp.get('p99', 0)]

x = np.arange(len(qp_metrics))
width = 0.35

ax2.bar(x - width/2, baseline_vals, width, label='Baseline', color='#3498db', alpha=0.8)
ax2.bar(x + width/2, intercepted_vals, width, label='With Intercept', color='#e74c3c', alpha=0.8)

ax2.set_ylabel('Latency (microseconds)', fontsize=12)
ax2.set_title('EXP-1: QP Creation Latency Distribution', fontsize=14, fontweight='bold')
ax2.set_xticks(x)
ax2.set_xticklabels(qp_metrics)
ax2.legend()
ax2.grid(axis='y', alpha=0.3)

# ===== 图3: MR注册延迟分布 =====
ax3 = axes[1, 0]

mr_metrics = ['MEAN', 'P50', 'P95', 'P99']
baseline_mr_vals = [baseline_mr.get('mean', 0), baseline_mr.get('p50', 0), 
                    baseline_mr.get('p95', 0), baseline_mr.get('p99', 0)]
intercepted_mr_vals = [intercepted_mr.get('mean', 0), intercepted_mr.get('p50', 0), 
                       intercepted_mr.get('p95', 0), intercepted_mr.get('p99', 0)]

x = np.arange(len(mr_metrics))
width = 0.35

ax3.bar(x - width/2, baseline_mr_vals, width, label='Baseline', color='#3498db', alpha=0.8)
ax3.bar(x + width/2, intercepted_mr_vals, width, label='With Intercept', color='#e74c3c', alpha=0.8)

ax3.set_ylabel('Latency (microseconds)', fontsize=12)
ax3.set_title('EXP-1: MR Registration Latency Distribution', fontsize=14, fontweight='bold')
ax3.set_xticks(x)
ax3.set_xticklabels(mr_metrics)
ax3.legend()
ax3.grid(axis='y', alpha=0.3)

# ===== 图4: 详细数据表格 =====
ax4 = axes[1, 1]
ax4.axis('off')

# 计算开销
qp_overhead = (intercepted_qp.get('mean', 0) - baseline_qp.get('mean', 0)) / baseline_qp.get('mean', 1) * 100 if baseline_qp.get('mean', 0) > 0 else 0
mr_overhead = (intercepted_mr.get('mean', 0) - baseline_mr.get('mean', 0)) / baseline_mr.get('mean', 1) * 100 if baseline_mr.get('mean', 0) > 0 else 0
qpd_overhead = (intercepted_qpd.get('mean', 0) - baseline_qpd.get('mean', 0)) / baseline_qpd.get('mean', 1) * 100 if baseline_qpd.get('mean', 0) > 0 else 0

# 创建表格数据
table_data = [
    ['Metric', 'Baseline', 'Intercepted', 'Overhead'],
    ['QP Create MEAN', f"{baseline_qp.get('mean', 0):.1f}μs", f"{intercepted_qp.get('mean', 0):.1f}μs", f"{qp_overhead:+.1f}%"],
    ['QP Create P95', f"{baseline_qp.get('p95', 0):.1f}μs", f"{intercepted_qp.get('p95', 0):.1f}μs", "-"],
    ['QP Create P99', f"{baseline_qp.get('p99', 0):.1f}μs", f"{intercepted_qp.get('p99', 0):.1f}μs", "-"],
    ['QP Destroy MEAN', f"{baseline_qpd.get('mean', 0):.1f}μs", f"{intercepted_qpd.get('mean', 0):.1f}μs", f"{qpd_overhead:+.1f}%"],
    ['MR Reg MEAN', f"{baseline_mr.get('mean', 0):.1f}μs", f"{intercepted_mr.get('mean', 0):.1f}μs", f"{mr_overhead:+.1f}%"],
    ['MR Reg P95', f"{baseline_mr.get('p95', 0):.1f}μs", f"{intercepted_mr.get('p95', 0):.1f}μs", "-"],
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

plt.suptitle('EXP-1: Microbenchmark Results (New Machines)', 
             fontsize=16, fontweight='bold', y=0.98)
plt.tight_layout()
plt.savefig('figures/exp1_microbenchmark.png', dpi=300, bbox_inches='tight')
plt.savefig('figures/exp1_microbenchmark.pdf', bbox_inches='tight')
print("✓ EXP-1图表已保存: figures/exp1_microbenchmark.png")
plt.close()

print("\n" + "="*60)
print("EXP-1: 微基准测试 - 完成!")
print("="*60)
print(f"\n各操作开销:")
print(f"  QP创建: {qp_overhead:+.1f}%")
print(f"  QP销毁: {qpd_overhead:+.1f}%")
print(f"  MR注册: {mr_overhead:+.1f}%")
