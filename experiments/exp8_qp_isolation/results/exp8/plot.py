#!/usr/bin/env python3
"""
EXP-8: QP隔离验证可视化 (Clean version - no summary box)
"""

import matplotlib.pyplot as plt
import numpy as np

# 数据
qp_data = [
    (0, 1, 26316.27),   # 第1个 - 冷启动
    (1, 1, 514.44),
    (2, 1, 568.52),
    (3, 1, 470.69),
    (4, 1, 579.40),
    (5, 1, 609.32),
    (6, 1, 552.33),
    (7, 1, 570.69),
    (8, 1, 550.85),
    (9, 1, 597.47),     # 第10个 - 配额用完
    (10, 0, 1.43),      # 被拦截 - 非常快
    (11, 0, 1.12),
    (12, 0, 0.94),
    (13, 0, 1.04),
    (14, 0, 0.95),
    (15, 0, 0.94),
    (16, 0, 0.95),
    (17, 0, 1.19),
    (18, 0, 0.94),
    (19, 0, 0.94),
]

quota = 10

qp_ids = [d[0] for d in qp_data]
successes = [d[1] for d in qp_data]
latencies_us = [d[2] for d in qp_data]
latencies_ms = [l/1000 for l in latencies_us]

fig, axes = plt.subplots(2, 1, figsize=(14, 9))

# ===== 图1: 成功率展示（带配额线）=====
ax1 = axes[0]

# 计算累积成功数
cumulative_success = []
count = 0
for s in successes:
    if s:
        count += 1
    cumulative_success.append(count)

# 使用阶梯图
ax1.step(qp_ids, cumulative_success, where='post', linewidth=3, color='#2ecc71', alpha=0.9)

# 填充区域
ax1.fill_between(qp_ids, 0, cumulative_success, alpha=0.3, color='green', step='post')

# 配额线
ax1.axhline(y=quota, color='#e74c3c', linestyle='--', linewidth=2.5, label=f'Quota Limit = {quota}')
ax1.axvline(x=quota-0.5, color='#3498db', linestyle=':', linewidth=2, label='Quota Exhausted')

# 标记区域
ax1.fill_between([0, quota-0.5], 0, quota+1, alpha=0.05, color='green')
ax1.fill_between([quota-0.5, len(qp_ids)], 0, quota+1, alpha=0.05, color='red')

# 数据点
for i, (qp_id, success, cum) in enumerate(zip(qp_ids, successes, cumulative_success)):
    color = '#2ecc71' if success else '#e74c3c'
    marker = 'o' if success else 'x'
    size = 100 if success else 120
    ax1.scatter(qp_id, cum, c=color, marker=marker, s=size, zorder=5, edgecolors='white', linewidth=1.5)

# 标注
ax1.annotate(f'Allowed\n{cumulative_success[quota-1]} QPs', xy=(4, 8), 
            fontsize=11, ha='center', color='green', fontweight='bold',
            bbox=dict(boxstyle='round', facecolor='lightgreen', alpha=0.8))
ax1.annotate(f'Blocked\n{len(qp_ids) - cumulative_success[-1]} QPs', xy=(15, 5), 
            fontsize=11, ha='center', color='red', fontweight='bold',
            bbox=dict(boxstyle='round', facecolor='lightcoral', alpha=0.8))

ax1.set_xlabel('QP Creation Attempt', fontsize=13)
ax1.set_ylabel('Cumulative Successful QPs', fontsize=13)
ax1.set_title('EXP-8: QP Resource Isolation Verification\nQuota=10 QPs, 20 Attempts', 
              fontsize=15, fontweight='bold')
ax1.set_xlim(-0.5, len(qp_ids) - 0.5)
ax1.set_ylim(0, quota + 2)
ax1.set_xticks(qp_ids)
ax1.legend(loc='lower right')
ax1.grid(True, alpha=0.3)

# ===== 图2: 延迟对比（冷启动 vs 正常创建 vs 拦截）=====
ax2 = axes[1]

# 分离三类延迟
cold_start = [latencies_ms[0]]  # 只有第1个
normal_creation = [l for i, l in enumerate(latencies_ms) if successes[i] and i > 0]  # 2-10
intercepted = [l for i, l in enumerate(latencies_ms) if not successes[i]]  # 11-20

# 绘制柱状图
bar_colors = []
for i, (success, l) in enumerate(zip(successes, latencies_ms)):
    if i == 0:
        bar_colors.append('#e67e22')  # 橙色 - 冷启动
    elif success:
        bar_colors.append('#2ecc71')  # 绿色 - 正常创建
    else:
        bar_colors.append('#e74c3c')  # 红色 - 被拦截

bars = ax2.bar(qp_ids, latencies_ms, color=bar_colors, alpha=0.85, edgecolor='black', linewidth=0.5)

# 配额线
ax2.axvline(x=quota-0.5, color='#3498db', linestyle=':', linewidth=2, label=f'Quota = {quota}')

# 标注冷启动
ax2.annotate(f'Cold Start\n{latencies_ms[0]:.1f} ms\n(First QP)', 
            xy=(0, latencies_ms[0]), xytext=(2.5, latencies_ms[0] * 0.75),
            fontsize=10, ha='center',
            arrowprops=dict(arrowstyle='->', color='#e67e22', lw=2),
            bbox=dict(boxstyle='round', facecolor='#ffe5cc', edgecolor='#e67e22', alpha=0.9))

# 标注正常创建区域
avg_normal = np.mean(normal_creation)
ax2.axhline(y=avg_normal, color='#2ecc71', linestyle='--', linewidth=1.5, alpha=0.8)
ax2.fill_between([0.5, quota-0.5], 0, avg_normal * 3, alpha=0.05, color='green')
ax2.annotate(f'Normal Creation\nAvg: {avg_normal:.2f} ms', 
            xy=(5, avg_normal * 2), fontsize=10, ha='center', color='green',
            bbox=dict(boxstyle='round', facecolor='lightgreen', alpha=0.7))

# 标注被拦截区域
avg_intercept = np.mean(intercepted)
ax2.fill_between([quota-0.5, len(qp_ids)-0.5], 0, 2, alpha=0.05, color='red')
ax2.annotate(f'Intercepted\nAvg: {avg_intercept*1000:.0f} ns', 
            xy=(15, 1), fontsize=10, ha='center', color='red',
            bbox=dict(boxstyle='round', facecolor='lightcoral', alpha=0.8))

ax2.set_xlabel('QP Creation Attempt', fontsize=13)
ax2.set_ylabel('Latency (milliseconds)', fontsize=13)
ax2.set_title('EXP-8: QP Creation Latency - Cold Start vs Normal vs Intercepted', 
              fontsize=15, fontweight='bold')
ax2.set_xticks(qp_ids)
ax2.set_ylim(0, max(latencies_ms) + 3)
ax2.legend(loc='upper right')
ax2.grid(axis='y', alpha=0.3)

# 添加图例说明
from matplotlib.patches import Patch
legend_elements = [
    Patch(facecolor='#e67e22', alpha=0.85, label=f'Cold Start (1st): {latencies_ms[0]:.1f} ms'),
    Patch(facecolor='#2ecc71', alpha=0.85, label=f'Normal (2nd-10th): {avg_normal:.2f} ms'),
    Patch(facecolor='#e74c3c', alpha=0.85, label=f'Intercepted: {avg_intercept*1000:.0f} ns'),
]
ax2.legend(handles=legend_elements, loc='upper right', fontsize=10)

plt.tight_layout()
plt.savefig('exp8_qp_isolation.png', dpi=300, bbox_inches='tight')
plt.savefig('exp8_qp_isolation.pdf', bbox_inches='tight')
print("✓ EXP-8图表已保存: figures/exp8_qp_isolation.png")
plt.close()

print("\n" + "="*60)
print("EXP-8: QP隔离验证 - 完成!")
print("="*60)
print(f"\n延迟分析:")
print(f"  冷启动 (QP #0): {latencies_ms[0]:.2f} ms")
print(f"  正常创建 (QP #1-9): {avg_normal:.2f} ms avg")
print(f"  被拦截 (QP #10-19): {avg_intercept*1000:.0f} ns avg")
print(f"\n✓ 隔离验证成功: 10个被拦截，配额限制严格执行!")
