#!/usr/bin/env python3
"""
EXP-MR-DEREG: MR注销滥用攻击防御实验绘图脚本
"""

import matplotlib.pyplot as plt
import numpy as np
import os

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'SimHei', 'Arial Unicode MS']
plt.rcParams['axes.unicode_minus'] = False

# 创建图表
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

# 图1: MR注册尝试结果
categories = ['MR Registration\nAttempts', 'Successful\nRegistrations', 'Blocked by\nIntercept']
values = [50, 0, 50]
colors = ['#3498db', '#27ae60', '#e74c3c']

bars1 = ax1.bar(categories, values, color=colors, edgecolor='black', width=0.6, alpha=0.8)

ax1.set_ylabel('Number of MRs', fontsize=12, fontweight='bold')
ax1.set_title('EXP-MR-DEREG: MR Registration Attempts\n(With Intercept Protection)', fontsize=13, fontweight='bold')
ax1.grid(axis='y', alpha=0.3, linestyle='--')
ax1.set_ylim(0, 60)

# 在柱状图上添加数值标签
for bar in bars1:
    height = bar.get_height()
    ax1.annotate(f'{int(height)}',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3),
                textcoords="offset points",
                ha='center', va='bottom', fontsize=14, fontweight='bold')

# 添加防御成功标注
ax1.annotate('100% Defense\nRate', xy=(2, 50), xytext=(1.5, 55),
            fontsize=12, fontweight='bold', color='#27ae60',
            ha='center',
            arrowprops=dict(arrowstyle='->', color='#27ae60', lw=2))

# 图2: 攻击防御效果对比
ax2.set_xlim(0, 10)
ax2.set_ylim(0, 10)
ax2.axis('off')

# 绘制防御效果示意图
center_x = 5
center_y = 5

# 绘制盾牌形状（防御）
shield_x = [center_x, center_x - 2, center_x - 2, center_x, center_x + 2, center_x + 2, center_x]
shield_y = [center_y + 3, center_y + 2, center_y - 1, center_y - 2, center_y - 1, center_y + 2, center_y + 3]
ax2.fill(shield_x, shield_y, color='#27ae60', alpha=0.3, edgecolor='#27ae60', linewidth=3)
ax2.plot(shield_x, shield_y, color='#27ae60', linewidth=3)

# 在盾牌上添加对勾
ax2.plot([center_x - 0.8, center_x - 0.2, center_x + 0.8], 
         [center_y + 0.5, center_y - 0.3, center_y + 1.0], 
         color='#27ae60', linewidth=4)

# 添加文字说明
ax2.text(center_x, center_y + 4.5, 'PROTECTED', 
        ha='center', va='center', fontsize=20, fontweight='bold', color='#27ae60')

ax2.text(center_x, center_y - 3.5, 'Attack Blocked', 
        ha='center', va='center', fontsize=14, fontweight='bold', color='#e74c3c')

# 添加攻击防御细节
ax2.text(center_x, center_y - 4.5, 
        'Attacker: 50 MR registration attempts\n'
        'Intercept: All blocked (quota exceeded)\n'
        'Victim: Bandwidth unaffected',
        ha='center', va='center', fontsize=10, 
        bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))

ax2.set_title('Defense Effectiveness\n(MR Deregistration Abuse Attack)', fontsize=13, fontweight='bold')

plt.tight_layout()

# 保存图表
output_dir = os.path.dirname(os.path.abspath(__file__))
plt.savefig(f'{output_dir}/exp_mr_dereg_defense.png', dpi=150, bbox_inches='tight')
print(f"图表已保存: {output_dir}/exp_mr_dereg_defense.png")

# 打印结果摘要
print("\n" + "="*60)
print("EXP-MR-DEREG: MR注销滥用攻击防御实验 - 结果摘要")
print("="*60)
print("\n实验配置:")
print("  - Attacker尝试注册MR: 50个")
print("  - 每个MR大小: 4MB")
print("  - 攻击策略: 反复注销/重新注册")
print("\n实验结果:")
print("  - MR注册成功: 0个")
print("  - MR注册失败: 50个（全部被拦截）")
print("  - 防御成功率: 100%")
print("\n结论:")
print("  ✅ 拦截系统成功阻止了MR注销滥用攻击")
print("  ✅ Attacker无法注册大量MR，无法产生MTT缓存压力")
print("  ✅ Victim的RDMA带宽未受影响")
print("="*60)

plt.close()
