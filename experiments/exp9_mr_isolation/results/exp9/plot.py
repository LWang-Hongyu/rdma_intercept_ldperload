#!/usr/bin/env python3
"""
EXP-9: MR资源隔离验证可视化
"""

import matplotlib.pyplot as plt
import numpy as np

# 数据
mr_data = [
    (0, 1, 88.00),     # 第1个 - 稍高延迟
    (1, 1, 30.67),
    (2, 1, 26.91),
    (3, 1, 25.18),
    (4, 1, 27.08),
    (5, 1, 25.17),
    (6, 1, 25.10),
    (7, 1, 25.62),
    (8, 1, 25.09),
    (9, 1, 24.07),     # 第10个 - 配额用完
    (10, 0, 6.98),     # 被拦截
    (11, 0, 5.33),
    (12, 0, 6.41),
    (13, 0, 4.74),
    (14, 0, 4.88),
    (15, 0, 6.29),
    (16, 0, 4.07),
    (17, 0, 5.13),
    (18, 0, 6.42),
    (19, 0, 4.89),
]

quota = 10

mr_ids = [d[0] for d in mr_data]
successes = [d[1] for d in mr_data]
latencies_us = [d[2] for d in mr_data]

fig, axes = plt.subplots(2, 1, figsize=(14, 9))

# ===== 图1: 累积成功MR数 =====
ax1 = axes[0]

# 计算累积成功数
cumulative_success = []
count = 0
for s in successes:
    if s:
        count += 1
    cumulative_success.append(count)

# 绘制阶梯图
ax1.step(mr_ids, cumulative_success, where='post', linewidth=3, color='#2ecc71', alpha=0.9)
ax1.fill_between(mr_ids, 0, cumulative_success, alpha=0.3, color='green', step='post')

# 配额线
ax1.axhline(y=quota, color='#e74c3c', linestyle='--', linewidth=2.5, label=f'Quota Limit = {quota}')
ax1.axvline(x=quota-0.5, color='#3498db', linestyle=':', linewidth=2, label='Quota Exhausted')

# 区域标注
ax1.fill_between([0, quota-0.5], 0, quota+1, alpha=0.05, color='green')
ax1.fill_between([quota-0.5, len(mr_ids)], 0, quota+1, alpha=0.05, color='red')

# 数据点
for i, (mr_id, success, cum) in enumerate(zip(mr_ids, successes, cumulative_success)):
    color = '#2ecc71' if success else '#e74c3c'
    marker = 'o' if success else 'x'
    size = 100 if success else 120
    ax1.scatter(mr_id, cum, c=color, marker=marker, s=size, zorder=5, edgecolors='white', linewidth=1.5)

# 标注
ax1.annotate(f'Allowed\n{cumulative_success[quota-1]} MRs', xy=(4, 7.5), 
            fontsize=11, ha='center', color='green', fontweight='bold',
            bbox=dict(boxstyle='round', facecolor='lightgreen', alpha=0.8))
ax1.annotate(f'Blocked\n{len(mr_ids) - cumulative_success[-1]} MRs', xy=(15, 5), 
            fontsize=11, ha='center', color='red', fontweight='bold',
            bbox=dict(boxstyle='round', facecolor='lightcoral', alpha=0.8))

ax1.set_xlabel('MR Registration Attempt', fontsize=13)
ax1.set_ylabel('Cumulative Successful MRs', fontsize=13)
ax1.set_title('EXP-9: MR Resource Isolation Verification\nQuota=10 MRs, 20 Attempts (4KB each)', 
              fontsize=15, fontweight='bold')
ax1.set_xlim(-0.5, len(mr_ids) - 0.5)
ax1.set_ylim(0, quota + 2)
ax1.set_xticks(mr_ids)
ax1.legend(loc='lower right')
ax1.grid(True, alpha=0.3)

# ===== 图2: MR注册延迟 =====
ax2 = axes[1]

# 分离成功和失败的延迟
success_latencies = [l for i, l in enumerate(latencies_us) if successes[i]]
fail_latencies = [l for i, l in enumerate(latencies_us) if not successes[i]]

# 颜色
colors = ['#2ecc71' if s else '#e74c3c' for s in successes]
bars = ax2.bar(mr_ids, latencies_us, color=colors, alpha=0.8, edgecolor='black', linewidth=0.5)

# 配额线
ax2.axvline(x=quota-0.5, color='#3498db', linestyle=':', linewidth=2, label=f'Quota = {quota}')

# 标注第一个MR（稍高延迟）
ax2.annotate(f'1st MR\n{latencies_us[0]:.1f}μs', 
            xy=(0, latencies_us[0]), xytext=(2, latencies_us[0] + 10),
            fontsize=10, ha='center',
            arrowprops=dict(arrowstyle='->', color='#e67e22', lw=1.5),
            bbox=dict(boxstyle='round', facecolor='#ffe5cc', edgecolor='#e67e22', alpha=0.8))

# 标注正常注册延迟
avg_success = np.mean(success_latencies[1:])  # 排除第一个
ax2.axhline(y=avg_success, color='#2ecc71', linestyle='--', linewidth=1.5, alpha=0.8)
ax2.annotate(f'Normal Avg: {avg_success:.1f}μs', 
            xy=(4.5, avg_success + 5), fontsize=10, ha='center', color='green',
            bbox=dict(boxstyle='round', facecolor='lightgreen', alpha=0.7))

# 标注被拦截延迟
avg_fail = np.mean(fail_latencies)
ax2.fill_between([quota-0.5, len(mr_ids)-0.5], 0, avg_fail * 2, alpha=0.05, color='red')
ax2.annotate(f'Intercepted\nAvg: {avg_fail:.1f}μs', 
            xy=(15, avg_fail + 5), fontsize=10, ha='center', color='red',
            bbox=dict(boxstyle='round', facecolor='lightcoral', alpha=0.8))

ax2.set_xlabel('MR Registration Attempt', fontsize=13)
ax2.set_ylabel('Latency (microseconds)', fontsize=13)
ax2.set_title('EXP-9: MR Registration Latency - Success vs Intercepted', 
              fontsize=15, fontweight='bold')
ax2.set_xticks(mr_ids)
ax2.set_ylim(0, max(latencies_us) + 15)
ax2.legend(loc='upper right')
ax2.grid(axis='y', alpha=0.3)

# 添加图例说明
from matplotlib.patches import Patch
legend_elements = [
    Patch(facecolor='#2ecc71', alpha=0.8, label=f'Success (1st): {latencies_us[0]:.1f}μs'),
    Patch(facecolor='#2ecc71', alpha=0.8, label=f'Success (2nd-10th): {avg_success:.1f}μs'),
    Patch(facecolor='#e74c3c', alpha=0.8, label=f'Intercepted: {avg_fail:.1f}μs'),
]
ax2.legend(handles=legend_elements, loc='upper right', fontsize=10)

plt.tight_layout()
plt.savefig('exp9_mr_isolation.png', dpi=300, bbox_inches='tight')
plt.savefig('exp9_mr_isolation.pdf', bbox_inches='tight')
print("✓ EXP-9图表已保存: figures/exp9_mr_isolation.png")
plt.close()

print("\n" + "="*60)
print("EXP-9: MR隔离验证 - 完成!")
print("="*60)
print(f"\n延迟分析:")
print(f"  第1个MR: {latencies_us[0]:.1f} μs (初始化)")
print(f"  正常注册: {avg_success:.1f} μs avg")
print(f"  被拦截: {avg_fail:.1f} μs avg")
print(f"\n✓ MR隔离验证成功: {cumulative_success[-1]}/{quota} 成功, {len(mr_ids)-cumulative_success[-1]} 被拦截!")
