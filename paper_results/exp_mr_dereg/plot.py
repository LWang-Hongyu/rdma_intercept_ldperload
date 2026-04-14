#!/usr/bin/env python3
"""
EXP-MR-DEREG: 统一画图脚本

用法:
    python3 plot.py [impact|motivation|all]
    
示例:
    python3 plot.py impact      # 生成攻击影响图
    python3 plot.py motivation  # 生成动机说明图
    python3 plot.py all         # 生成所有图表（默认）
"""

import sys
import os
import subprocess

def print_usage():
    print(__doc__)
    print("\n可用的图表:")
    print("  impact     - 攻击对Victim带宽影响图")
    print("  motivation - 攻击动机说明图")
    print("  all        - 生成所有图表")

def main():
    chart = sys.argv[1] if len(sys.argv) > 1 else 'all'
    
    analysis_dir = 'analysis'
    
    if chart == 'impact':
        print("Generating: fig_mr_dereg_impact.png")
        subprocess.run(['python3', f'{analysis_dir}/plot_mr_dereg_impact.py'])
        
    elif chart == 'motivation':
        print("Generating: fig_mr_dereg_motivation.png")
        subprocess.run(['python3', f'{analysis_dir}/plot_mr_dereg_motivation.py'])
        
    elif chart == 'all':
        print("Generating all charts...")
        print("1. fig_mr_dereg_impact.png")
        subprocess.run(['python3', f'{analysis_dir}/plot_mr_dereg_impact.py'])
        print("2. fig_mr_dereg_motivation.png")
        subprocess.run(['python3', f'{analysis_dir}/plot_mr_dereg_motivation.py'])
        print("\nAll charts generated!")
        
    else:
        print(f"Unknown chart: {chart}")
        print_usage()
        return 1
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
