#!/usr/bin/env python3
"""
分析Victim带宽数据并生成可视化图表
"""

import sys
import csv
import os

def analyze(input_file, output_plot=None):
    times = []
    bandwidths = []
    phases = []
    
    # 读取CSV数据
    with open(input_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            times.append(float(row['timestamp']))
            bandwidths.append(float(row['bandwidth_gbps']))
            phases.append(int(row['phase']))
    
    if not times:
        print("ERROR: No data found in input file")
        return
    
    # 分段统计
    baseline = [b for b, p in zip(bandwidths, phases) if p == 0]
    attack = [b for b, p in zip(bandwidths, phases) if p == 1]
    
    baseline_avg = sum(baseline) / len(baseline) if baseline else 0
    attack_avg = sum(attack) / len(attack) if attack else 0
    
    # 计算标准差
    import statistics
    baseline_std = statistics.stdev(baseline) if len(baseline) > 1 else 0
    attack_std = statistics.stdev(attack) if len(attack) > 1 else 0
    
    print(f"========================================")
    print(f"Results: {os.path.basename(input_file)}")
    print(f"========================================")
    print(f"Samples:          {len(times)} points")
    print(f"Baseline (0-5s):  {baseline_avg:.2f} ± {baseline_std:.2f} Gbps (n={len(baseline)})")
    if attack:
        print(f"Attack phase:     {attack_avg:.2f} ± {attack_std:.2f} Gbps (n={len(attack)})")
        degradation = (baseline_avg - attack_avg) / baseline_avg * 100
        print(f"Degradation:      {degradation:.1f}%")
        
        if degradation > 30:
            print(f"Status:           ❌ VULNERABLE (attack effective)")
        elif degradation > 10:
            print(f"Status:           ⚠️  PARTIAL (some impact)")
        else:
            print(f"Status:           ✅ PROTECTED (attack ineffective)")
    else:
        print(f"No attack phase data")
    print(f"========================================")
    
    # 生成图表
    if output_plot:
        try:
            import matplotlib.pyplot as plt
            
            plt.figure(figsize=(12, 7))
            
            # 基线阶段（绿色）
            base_t = [t for t, p in zip(times, phases) if p == 0]
            base_b = [b for b, p in zip(bandwidths, phases) if p == 0]
            plt.scatter(base_t, base_b, c='green', s=15, alpha=0.5, label=f'Baseline Phase (0-5s)')
            
            # 攻击阶段（红色）
            atk_t = [t for t, p in zip(times, phases) if p == 1]
            atk_b = [b for b, p in zip(bandwidths, phases) if p == 1]
            if atk_t:
                plt.scatter(atk_t, atk_b, c='red', s=15, alpha=0.5, label=f'Attack Phase (5-30s)')
            
            # 平均带宽线
            if baseline:
                plt.axhline(y=baseline_avg, color='green', linestyle='--', linewidth=2,
                           label=f'Baseline Avg: {baseline_avg:.1f} Gbps')
            if attack:
                plt.axhline(y=attack_avg, color='red', linestyle='--', linewidth=2,
                           label=f'Attack Avg: {attack_avg:.1f} Gbps')
            
            # 阶段分界线
            plt.axvline(x=5, color='gray', linestyle=':', linewidth=2, label='Attack Start (5s)')
            
            plt.xlabel('Time (seconds)', fontsize=12)
            plt.ylabel('Bandwidth (Gbps)', fontsize=12)
            plt.title('Victim Bandwidth vs Time - MR Deregistration Abuse Attack\n' + 
                      f'({os.path.basename(input_file)})', fontsize=13)
            plt.legend(loc='upper right', fontsize=9)
            plt.grid(True, alpha=0.3)
            plt.xlim(-0.5, max(times) + 1)
            
            # 添加统计信息文本框
            if attack:
                degradation = (baseline_avg - attack_avg) / baseline_avg * 100
                textstr = f'Baseline: {baseline_avg:.1f} Gbps\nAttack: {attack_avg:.1f} Gbps\nDegradation: {degradation:.1f}%'
                props = dict(boxstyle='round', facecolor='wheat', alpha=0.8)
                plt.text(0.02, 0.15, textstr, transform=plt.gca().transAxes, fontsize=10,
                        verticalalignment='top', bbox=props)
            
            plt.tight_layout()
            plt.savefig(output_plot, dpi=150, bbox_inches='tight')
            print(f"\nPlot saved to: {output_plot}")
        except ImportError:
            print("\nWarning: matplotlib not available, skipping plot generation")
            print("Install with: pip3 install matplotlib")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input.csv> [output.png]")
        print(f"\nExample:")
        print(f"  {sys.argv[0]} victim_no_protection.csv victim_result.png")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_plot = sys.argv[2] if len(sys.argv) > 2 else None
    
    if not os.path.exists(input_file):
        print(f"ERROR: File not found: {input_file}")
        sys.exit(1)
    
    analyze(input_file, output_plot)
