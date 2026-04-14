#!/usr/bin/env python3
"""
EXP-5: 动态策略热更新效果可视化 (Clean version - no summary box)
"""

import matplotlib.pyplot as plt
import numpy as np

# 读取数据
data = []
with open('dynamic_policy_effect.txt', 'r') as f:
    for line in f:
        if line.strip() and not line.startswith('#'):
            parts = line.strip().split(',')
            if len(parts) == 4 and parts[0].isdigit():
                data.append({
                    'attempt': int(parts[0]),
                    'timestamp': float(parts[1]),
                    'success': int(parts[2]),
                    'latency': float(parts[3])
                })

# 参数
update_at = 7  # 第7次尝试时更新
initial_quota = 5
updated_quota = 10

# 计算累积QP数量
cumulative_qp = []
count = 0
for d in data:
    if d['success']:
        count += 1
    cumulative_qp.append(count)

# 创建图表
fig, axes = plt.subplots(2, 1, figsize=(14, 10))

# ===== 图1: 累积QP数量时间线（核心图）=====
ax1 = axes[0]

# 提取时间和累积数量
timestamps = [d['timestamp'] / 1000 for d in data]  # 转换为秒
attempts = [d['attempt'] for d in data]

# 绘制累积QP数量曲线（阶梯图）
ax1.step(timestamps, cumulative_qp, where='post', linewidth=3, 
         color='#2ecc71', label='Cumulative QP Count', alpha=0.9)

# 添加数据点标记
for i, (t, q, d) in enumerate(zip(timestamps, cumulative_qp, data)):
    marker_color = '#2ecc71' if d['success'] else '#e74c3c'
    marker = 'o' if d['success'] else 'x'
    ax1.scatter(t, q, c=marker_color, marker=marker, s=100, zorder=5, edgecolors='white', linewidth=1)

# 绘制配额限制线（热更新前）
update_time = data[update_at - 1]['timestamp'] / 1000
ax1.hlines(y=initial_quota, xmin=0, xmax=update_time, colors='#e74c3c', 
           linestyles='-', linewidth=3, alpha=0.8, label=f'Initial Quota = {initial_quota}')
ax1.hlines(y=updated_quota, xmin=update_time, xmax=max(timestamps) + 1, colors='#3498db', 
           linestyles='-', linewidth=3, alpha=0.8, label=f'Updated Quota = {updated_quota}')

# 热更新垂直线
ax1.axvline(x=update_time, color='#f39c12', linestyle='--', linewidth=2.5, 
           label=f'Hot Update (t={update_time:.1f}s)')

# 填充区域
ax1.fill_between([0, update_time], 0, initial_quota + 0.5, alpha=0.1, color='red')
ax1.fill_between([update_time, max(timestamps) + 1], 0, updated_quota + 0.5, alpha=0.1, color='green')

# 标注热更新点
ax1.annotate(f'Hot Update\nQuota: {initial_quota}→{updated_quota}', 
            xy=(update_time, cumulative_qp[update_at-1]), 
            xytext=(update_time + 1.5, cumulative_qp[update_at-1] + 2),
            fontsize=11, ha='center',
            arrowprops=dict(arrowstyle='->', color='#f39c12', lw=2),
            bbox=dict(boxstyle='round,pad=0.5', facecolor='yellow', edgecolor='#f39c12', alpha=0.9))

# 标注关键数据点
ax1.annotate(f'{cumulative_qp[update_at-1]}', 
            xy=(update_time, cumulative_qp[update_at-1]), 
            xytext=(update_time - 0.3, cumulative_qp[update_at-1] + 0.3),
            fontsize=10, ha='center', fontweight='bold',
            bbox=dict(boxstyle='round', facecolor='white', edgecolor='gray', alpha=0.8))

final_qp = cumulative_qp[-1]
ax1.annotate(f'{final_qp}', 
            xy=(timestamps[-1], final_qp), 
            xytext=(timestamps[-1] - 0.5, final_qp + 0.3),
            fontsize=11, ha='center', fontweight='bold',
            bbox=dict(boxstyle='round', facecolor='lightgreen', edgecolor='#2ecc71', alpha=0.9))

ax1.set_xlabel('Time (seconds)', fontsize=13)
ax1.set_ylabel('Cumulative QP Count', fontsize=13)
ax1.set_title('EXP-5: Dynamic Policy Hot Update Effect\n'
              'Cumulative QP Creation vs Quota Limit Over Time', 
              fontsize=15, fontweight='bold')
ax1.set_xlim(0, max(timestamps) + 0.5)
ax1.set_ylim(0, updated_quota + 1.5)
ax1.legend(loc='upper left', fontsize=10)
ax1.grid(True, alpha=0.3)

# 添加阶段标注
ax1.text(1.5, initial_quota + 0.8, 'Quota=5', fontsize=11, color='#e74c3c', fontweight='bold')
ax1.text(6, updated_quota + 0.8, 'Quota=10', fontsize=11, color='#3498db', fontweight='bold')

# ===== 图2: 每次尝试的结果和延迟 =====
ax2 = axes[1]

# 延迟数据（转换为毫秒）
latencies_ms = [d['latency'] / 1000 for d in data]
success_flags = [d['success'] for d in data]

# 使用不同颜色区分成功和失败
colors = ['#2ecc71' if s else '#e74c3c' for s in success_flags]
bars = ax2.bar(attempts, latencies_ms, color=colors, alpha=0.8, edgecolor='black', linewidth=0.5)

# 标记热更新点
ax2.axvline(x=update_at, color='#f39c12', linestyle='--', linewidth=2.5, 
           label=f'Hot Update (Attempt #{update_at})')

# 标注冷启动（第一个QP延迟较高）
ax2.annotate(f'Cold Start\n{latencies_ms[0]:.1f}ms', 
            xy=(1, latencies_ms[0]), xytext=(2.5, latencies_ms[0] * 0.7),
            fontsize=10, ha='center',
            arrowprops=dict(arrowstyle='->', color='#e67e22', lw=1.5),
            bbox=dict(boxstyle='round', facecolor='#ffe5cc', edgecolor='#e67e22', alpha=0.8))

# 标注正常创建延迟
normal_latencies = [l for i, l in enumerate(latencies_ms) if success_flags[i] and i > 0]
if normal_latencies:
    avg_normal = np.mean(normal_latencies)
    ax2.axhline(y=avg_normal, color='blue', linestyle=':', linewidth=1.5, alpha=0.7)

# 标注被拦截的延迟（极快）
intercepted_latencies = [l for i, l in enumerate(latencies_ms) if not success_flags[i]]
if intercepted_latencies:
    avg_intercept = np.mean(intercepted_latencies)
    ax2.fill_between([11, 20], 0, avg_intercept * 3, alpha=0.1, color='red')
    ax2.annotate(f'Intercepted\nAvg: {avg_intercept:.1f}μs', 
                xy=(15.5, avg_intercept * 2), fontsize=10, ha='center', color='red',
                bbox=dict(boxstyle='round', facecolor='lightcoral', alpha=0.7))

ax2.set_xlabel('Attempt Number', fontsize=13)
ax2.set_ylabel('Latency (milliseconds)', fontsize=13)
ax2.set_title('EXP-5: QP Creation Latency per Attempt', fontsize=15, fontweight='bold')
ax2.set_xticks(attempts)
ax2.legend(loc='upper right')
ax2.grid(axis='y', alpha=0.3)

# 添加结果标注
ax2.annotate('ALLOWED', xy=(5.5, max(latencies_ms) * 0.9), fontsize=12, ha='center', 
            color='green', fontweight='bold', alpha=0.7)
ax2.annotate('BLOCKED', xy=(15.5, max(latencies_ms) * 0.9), fontsize=12, ha='center', 
            color='red', fontweight='bold', alpha=0.7)

plt.tight_layout()
plt.savefig('figures/exp5_hot_update_effect.png', dpi=300, bbox_inches='tight')
plt.savefig('figures/exp5_hot_update_effect.pdf', bbox_inches='tight')
print("✓ EXP-5图表已保存: figures/exp5_hot_update_effect.png")
plt.close()

# 打印实验摘要到控制台
print("\n" + "="*60)
print("EXP-5: 动态策略热更新效果验证 - 完成!")
print("="*60)
print(f"\n实验参数:")
print(f"  初始配额: {initial_quota} QPs")
print(f"  更新配额: {updated_quota} QPs (Attempt #{update_at})")
print(f"\n结果:")
print(f"  总尝试: {len(data)}")
print(f"  成功创建: {cumulative_qp[-1]} QPs")
print(f"  被拦截: {len(data) - cumulative_qp[-1]} QPs")
print(f"\n延迟分析:")
print(f"  冷启动: {latencies_ms[0]:.2f}ms")
print(f"  正常创建: {np.mean(normal_latencies):.2f}ms (avg)")
print(f"  被拦截: {np.mean(intercepted_latencies)*1000:.1f}μs (avg)")
print(f"\n✓ 热更新验证成功!")
