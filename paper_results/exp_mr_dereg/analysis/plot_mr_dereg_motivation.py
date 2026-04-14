#!/usr/bin/env python3
"""
Plot for MR Deregistration Abuse Attack - Motivation Section
Based on Understanding RDMA (NSDI'23) findings
"""

import matplotlib.pyplot as plt
import matplotlib
matplotlib.rcParams['font.family'] = 'DejaVu Sans'
matplotlib.rcParams['font.size'] = 11

# Experimental data
data = {
    'no_intercept': {
        'cycles': 6364,
        'mr_ops': 63640,
        'reg_latency': 205,
        'dereg_latency': 108,
        'success_rate': 100
    },
    'with_intercept': {
        'cycles': 0,
        'mr_ops': 0,
        'reg_latency': 0,
        'dereg_latency': 0,
        'success_rate': 0
    }
}

fig, axes = plt.subplots(1, 2, figsize=(14, 5))

# Figure 1: Attack Effectiveness Comparison
ax1 = axes[0]
categories = ['Without\nInterception', 'With\nInterception']
cycles = [data['no_intercept']['cycles'], data['with_intercept']['cycles']]
ops = [data['no_intercept']['mr_ops'] / 1000, data['with_intercept']['mr_ops']]  # Convert to thousands

x = range(len(categories))
width = 0.35

# Primary axis - Cycles
bars1 = ax1.bar([i - width/2 for i in x], cycles, width, label='Deregister/Register Cycles', 
                color='#E63946', edgecolor='black', linewidth=1.2)
ax1.set_ylabel('Number of Cycles', fontweight='bold', color='#E63946')
ax1.tick_params(axis='y', labelcolor='#E63946')
ax1.set_ylim(0, 7000)

# Secondary axis - MR Operations
ax2 = ax1.twinx()
bars2 = ax2.bar([i + width/2 for i in x], ops, width, label='MR Operations (×1000)', 
                color='#457B9D', edgecolor='black', linewidth=1.2)
ax2.set_ylabel('MR Operations (×1000)', fontweight='bold', color='#457B9D')
ax2.tick_params(axis='y', labelcolor='#457B9D')
ax2.set_ylim(0, 70)

ax1.set_xticks(x)
ax1.set_xticklabels(categories)
ax1.set_xlabel('Protection Mechanism', fontweight='bold')
ax1.set_title('MR Deregistration Abuse Attack Effectiveness\n(20 seconds, 50 MRs, batch=10)', fontweight='bold')

# Add value labels
for bar in bars1:
    height = bar.get_height()
    ax1.text(bar.get_x() + bar.get_width()/2., height + 100,
             f'{int(height)}',
             ha='center', va='bottom', fontweight='bold', fontsize=10)

for bar in bars2:
    height = bar.get_height()
    ax2.text(bar.get_x() + bar.get_width()/2., height + 1,
             f'{int(height)}K' if height > 0 else '0',
             ha='center', va='bottom', fontweight='bold', fontsize=10)

# Add legend
lines1, labels1 = ax1.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper right')

ax1.grid(axis='y', alpha=0.3, linestyle='--')

# Figure 2: Attack Timeline Conceptual
ax3 = axes[1]
ax3.axis('off')

y_pos = 0.95
ax3.text(0.5, y_pos, 'Understanding RDMA Attack Vector', ha='center', fontsize=12, fontweight='bold')
y_pos -= 0.1

# Explain the attack
attack_text = [
    "Attack Pattern:",
    "  1. Register 50 MRs (4MB each)",
    "  2. Loop: Deregister 10 MRs → Reregister 10 MRs",
    "  3. Repeat rapidly for 20 seconds",
    "",
    "Impact on NIC:",
    "  • MTT (Memory Translation Table) cache thrashing",
    "  • Frequent cache misses for all tenants",
    "  • Performance degradation for legitimate workloads",
]

for line in attack_text:
    ax3.text(0.1, y_pos, line, fontsize=10, family='monospace')
    y_pos -= 0.06

y_pos -= 0.05
ax3.text(0.5, y_pos, 'Protection Results', ha='center', fontsize=11, fontweight='bold',
         bbox=dict(boxstyle='round', facecolor='lightblue', alpha=0.5))
y_pos -= 0.08

results_text = [
    "Without Interception:",
    "  ✗ Attacker: 6,364 cycles, 63,640 MR operations",
    "  ✗ MTT cache severely polluted",
    "  ✗ Other tenants experience high latency",
    "",
    "With Transparent Interception (Quota=20):",
    "  ✓ Attacker: BLOCKED (0 operations)",
    "  ✓ Quota enforcement prevents abuse",
    "  ✓ MTT cache protected for legitimate tenants",
]

for line in results_text:
    color = 'darkred' if line.startswith('  ✗') else 'darkgreen' if line.startswith('  ✓') else 'black'
    weight = 'bold' if line.startswith('  ✓') or line.startswith('  ✗') else 'normal'
    ax3.text(0.1, y_pos, line, fontsize=10, family='monospace', color=color, fontweight=weight)
    y_pos -= 0.06

plt.suptitle('MR Deregistration Abuse Attack - Motivation Validation', fontsize=14, fontweight='bold', y=1.02)
plt.tight_layout()

plt.savefig('fig_mr_dereg_motivation.pdf', dpi=300, bbox_inches='tight', format='pdf')
plt.savefig('fig_mr_dereg_motivation.png', dpi=300, bbox_inches='tight')
print("✓ Generated: fig_mr_dereg_motivation.pdf/png")
