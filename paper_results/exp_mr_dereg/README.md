# EXP-MR-DEREG: MR注销滥用攻击实验

本目录包含MR注销滥用攻击实验的完整代码、脚本和数据。

## 目录结构

```
exp_mr_dereg/
├── README.md                     # 本文件
│
├── 📁 src/                       # 源代码
│   ├── exp_mr_dereg_abuse.cpp    # 攻击程序源码
│   └── Makefile.mr_dereg         # 编译脚本
│
├── 📁 analysis/                  # 画图脚本
│   ├── plot_mr_dereg_impact.py   # 攻击影响可视化
│   ├── plot_mr_dereg_motivation.py
│   └── plot_mr_dereg.py
│
├── 📁 scripts/                   # 运行脚本
│   ├── exp_mr_dereg_with_victim.sh
│   ├── run_mr_dereg_comparison.sh
│   └── run_simple_mr_test.sh
│
├── 📁 results/                   # 实验结果数据
│   ├── no_intercept.log
│   ├── no_intercept_summary.txt
│   ├── with_intercept.log
│   └── with_intercept_summary.txt
│
├── 📁 figures/                   # 附加图表
│   └── mr_dereg_protection.png
│
├── 📁 docs/                      # 文档
│   ├── INDEX.md
│   ├── MOTIVATION_MR_DERG_V2.md
│   ├── ISOLATION_EXPERIMENTS.md
│   └── ISOLATION_EXPERIMENTS_GUIDE.md
│
├── fig_mr_dereg_impact.png       # ⭐ 主要图表
├── fig_mr_dereg_impact.pdf
├── fig_mr_dereg_motivation.png
└── fig_mr_dereg_motivation.pdf
```

## 快速使用

### 1. 编译攻击程序

```bash
cd src
make -f Makefile.mr_dereg
# 生成: exp_mr_dereg_abuse
```

### 2. 运行实验

```bash
# 使用自动化脚本
cd scripts
./run_mr_dereg_comparison.sh

# 或带Victim的完整测试
./exp_mr_dereg_with_victim.sh
```

### 3. 生成图表

```bash
# 进入分析目录
cd analysis

# 生成攻击影响图
python3 plot_mr_dereg_impact.py
# 输出: fig_mr_dereg_impact.png (在当前目录)

# 生成动机说明图
python3 plot_mr_dereg_motivation.py
# 输出: fig_mr_dereg_motivation.png
```

**注意**: 画图脚本会将图表保存到**当前目录**，运行后请手动移动到上级目录：
```bash
cd analysis
python3 plot_mr_dereg_impact.py
mv fig_mr_dereg_impact.png ../
```

### 4. 查看结果

```bash
# 查看数据
cat results/no_intercept_summary.txt
cat results/with_intercept_summary.txt

# 查看图表
# fig_mr_dereg_impact.png - 主要结果图
```

## 实验说明

### 攻击原理

MR注销滥用攻击通过反复注销/注册内存区域(Memory Region)，导致NIC的MTT缓存抖动：

1. **初始阶段**: 注册50个MR（每个4MB）
2. **攻击阶段**: 循环注销10个MR → 立即重新注册10个MR
3. **影响**: Victim的RDMA操作遭受缓存未命中，带宽下降40-60%

### 防御机制

拦截系统通过MR配额限制防御：
- 设置配额 = 20 MR/进程
- Attacker只能注册20个MR，无法产生足够缓存压力
- Victim带宽保持稳定

## 图表说明

| 文件 | 说明 |
|------|------|
| `fig_mr_dereg_impact.png` | 攻击对Victim带宽影响（核心图） |
| `fig_mr_dereg_motivation.png` | 攻击动机说明图 |

## 与其他目录的关系

```
experiments/exp_mr_dereg/     ← 主要实验环境
    ├── src/
    ├── run.sh                ← 推荐从这里运行
    └── results/              ← 实验结果

paper_results/exp_mr_dereg/   ← 本目录 (数据存档)
    ├── src/                  ← 源码备份
    ├── analysis/             ← 画图脚本
    ├── results/              ← 原始数据
    └── *.png                 ← 生成的图表
```

**推荐使用**: `experiments/exp_mr_dereg/run.sh` 运行完整实验流程。
