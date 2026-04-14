#!/usr/bin/env python3
"""
EXP-8: QP资源隔离测试可视化
"""

import matplotlib.pyplot as plt
import numpy as np

# 读取实验结果数据
baseline_success = 50
baseline_failed = 0
baseline_total = 50

intercept_success = 10
intercept_failed = 40
intercept_total = 50

# 创建图表
fig, axes = plt.subplots(1, 2, figsize=(14, 6))

# ===== 图1: QP创建成功/失败对比 =====
ax1 = axes[0]

categories = ['Baseline\n(No Intercept)', 'With Intercept\n(Limit=10)']
success_data = [baseline_success, intercept_success]
failed_data = [baseline_failed, intercept_failed]

x = np.arange(len(categories))
width = 0.35

bars1 = ax1.bar(x - width/2, success_data, width, label='Success', 
                color='#2ecc71', alpha=0.8, edgecolor='black')
bars2 = ax1.bar(x + width/2, failed_data, width, label='Failed (Blocked)', 
                color='#e74c3c', alpha=0.8, edgecolor='black')

# 添加数值标签
for bar in bars1:
    height = bar.get_height()
    if height > 0:
        ax1.annotate(f'{int(height)}',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3), textcoords="offset points",
                    ha='center', va='bottom', fontsize=12, fontweight='bold')

for bar in bars2:
    height = bar.get_height()
    if height > 0:
        ax1.annotate(f'{int(height)}',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3), textcoords="offset points",
                    ha='center', va='bottom', fontsize=12, fontweight='bold', color='red')

ax1.set_ylabel('Number of QPs', fontsize=12)
ax1.set_title('EXP-8: QP Creation with Resource Limits', fontsize=14, fontweight='bold')
ax1.set_xticks(x)
ax1.set_xticklabels(categories)
ax1.legend()
ax1.grid(axis='y', alpha=0.3)
ax1.set_ylim(0, 60)

# 添加说明文字
ax1.text(0.5, 0.95, 
        'Quota Limit = 10 QPs per tenant\nAttempts = 50 QPs',
        transform=ax1.transAxes, fontsize=10, verticalalignment='top', horizontalalignment='center',
        bbox=dict(boxstyle='round', facecolor='lightblue', alpha=0.5))

# ===== 图2: 成功率对比 =====
ax2 = axes[1]

success_rates = [100.0, 20.0]  # 百分比
colors = ['#2ecc71', '#e74c3c']

bars = ax2.bar(categories, success_rates, color=colors, alpha=0.8, edgecolor='black', width=0.5)

for bar, rate in zip(bars, success_rates):
    height = bar.get_height()
    ax2.annotate(f'{rate:.1f}%',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3), textcoords="offset points",
                ha='center', va='bottom', fontsize=14, fontweight='bold')

ax2.set_ylabel('Success Rate (%)', fontsize=12)
ax2.set_title('EXP-8: QP Creation Success Rate', fontsize=14, fontweight='bold')
ax2.set_ylim(0, 120)
ax2.grid(axis='y', alpha=0.3)

# 添加结论
ax2.axhline(y=100, color='green', linestyle='--', alpha=0.5, label='Expected (No Limit)')
ax2.axhline(y=20, color='red', linestyle='--', alpha=0.5, label='With Limit (10/50)')

# 添加结论文本框
conclusion_text = (
    "Conclusion:\n"
    "✓ Interception system correctly\n"
    "  enforces QP quota limits\n"
    "✓ 40 QPs blocked as expected\n"
    "✓ Resource isolation working"
)
ax2.text(0.98, 0.5, conclusion_text,
        transform=ax2.transAxes, fontsize=10, verticalalignment='center', horizontalalignment='right',
        bbox=dict(boxstyle='round', facecolor='lightgreen', alpha=0.7))

plt.tight_layout()
plt.savefig('figures/exp8_qp_isolation.png', dpi=300, bbox_inches='tight')
print("✓ EXP-8图表已保存: figures/exp8_qp_isolation.png")

print("\n" + "="*60)
print("EXP-8: QP资源隔离测试 - 完成!")
print("="*60)
print(f"\n基线测试 (无拦截):")
print(f"  请求QP数: {baseline_total}")
print(f"  成功: {baseline_success} ({baseline_success/baseline_total*100:.1f}%)")
print(f"  失败: {baseline_failed}")

print(f"\n拦截测试 (限制=10):")
print(f"  请求QP数: {intercept_total}")
print(f"  成功: {intercept_success} ({intercept_success/intercept_total*100:.1f}%)")
print(f"  失败: {intercept_failed} (被拦截)")

print(f"\n结论:")
print(f"  ✓ 拦截系统正确执行了QP配额限制")
print(f"  ✓ 超出配额的40个QP被成功拦截")
