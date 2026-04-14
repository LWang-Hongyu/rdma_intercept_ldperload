# RDMA Intercept 实验目录

本目录包含所有实验相关的代码、脚本和文档。每个实验有独立的子目录，包含完整的运行环境。

## 目录结构

```
experiments/
├── README.md                          # 本文件（总览）
├──
├── exp1_microbenchmark/               # EXP-1: 微基准测试
│   ├── README.md                      # 实验文档
│   ├── run.sh                         # 运行脚本
│   ├── src/                           # 源代码
│   ├── analysis/                      # 分析脚本
│   └── results/                       # ⭐ 实验结果（本地保存）
│       ├── baseline.csv
│       └── with_intercept.csv
│
├── exp2_multi_tenant_isolation/       # EXP-2: 多租户隔离
│   ├── README.md
│   ├── run.sh
│   ├── src/
│   ├── analysis/
│   └── results/                       # 实验结果
│
├── exp5_dynamic_policy/               # EXP-5: 动态策略
│   ├── README.md
│   ├── run.sh
│   ├── src/
│   ├── analysis/
│   └── results/                       # 实验结果
│
├── exp8_qp_isolation/                 # EXP-8: QP数量隔离
│   ├── README.md
│   ├── src/
│   ├── analysis/
│   └── results/                       # 实验结果
│
├── exp9_mr_isolation/                 # EXP-9: MR数量隔离
│   ├── README.md
│   ├── src/
│   ├── analysis/
│   └── results/                       # 实验结果
│
└── exp_mr_dereg/                      # EXP-MR-DEREG: 注销滥用攻击
    ├── README.md                      # 完整实验文档
    ├── QUICKSTART.md                  # 快速开始指南
    ├── run.sh                         # 一键运行脚本
    ├── src/                           # 源码
    │   ├── attacker.c
    │   └── victim_bw_monitor.py
    ├── analysis/
    │   └── analyze_bandwidth.py
    └── results/                       # ⭐ 实验结果
        ├── victim_no_protection.csv
        ├── victim_no_protection.png
        ├── victim_with_protection.csv
        └── victim_with_protection.png
```

## 快速导航

| 实验 | 描述 | 快速开始 |
|------|------|---------|
| EXP-1 | 微基准测试（QP/MR/CQ操作开销） | `cd exp1_microbenchmark && ./run.sh` |
| EXP-2 | 多租户隔离验证 | `cd exp2_multi_tenant_isolation && ./run.sh` |
| EXP-5 | 动态策略热更新 | `cd exp5_dynamic_policy && ./run.sh` |
| EXP-8 | QP数量隔离限制 | `cd exp8_qp_isolation && ./run.sh` |
| EXP-9 | MR数量隔离限制 | `cd exp9_mr_isolation && ./run.sh` |
| **EXP-MR-DEREG** | **MR注销滥用攻击（Victim带宽影响）** | `cd exp_mr_dereg && ./run.sh` |

## 结果位置

**⭐ 重要：实验结果直接保存在各自的 `results/` 目录下！**

例如运行 `exp_mr_dereg` 实验后：

```bash
$ ls exp_mr_dereg/results/
victim_no_protection.csv
victim_no_protection.png
victim_with_protection.csv
victim_with_protection.png
```

### 运行实验的一般流程

1. **进入实验目录**
   ```bash
   cd experiments/exp_XXX
   ```

2. **阅读实验文档**
   ```bash
   cat README.md
   # 或快速开始
   cat QUICKSTART.md  # 如果有的话
   ```

3. **运行实验**
   ```bash
   ./run.sh
   ```

4. **查看结果（在当前目录下）**
   ```bash
   ls results/
   cat results/xxx.csv
   # 查看图表
   # results/xxx.png
   ```

5. **生成图表（可选）**
   ```bash
   python3 analysis/plot.py
   ```

## 网络配置

```
Local  (guolab-8):  10.157.197.53  (RDMA: 192.168.108.2)
Remote (guolab-6):  10.157.197.51  (RDMA: 192.168.106.2)
```

确保SSH免密登录已配置：
```bash
ssh why@10.157.197.51 "hostname"
```

## 依赖检查

```bash
# 基础工具
which ib_write_bw     # perftest 包
which python3

# Python 包
python3 -c "import matplotlib"  # 画图
python3 -c "import numpy"       # 数值计算

# 编译工具
which gcc
```

## 常见问题

### Q: 实验结果在哪里？
**A:** 每个实验的结果保存在各自的 `results/` 子目录下，例如：
- `exp1_microbenchmark/results/`
- `exp_mr_dereg/results/`

### Q: 如何清理旧结果？
**A:** 
```bash
cd exp_XXX
rm -rf results/*
```

### Q: 如何备份所有实验结果？
**A:**
```bash
# 备份到paper_results（可选）
cp -r exp_*/results/* ../paper_results/ 2>/dev/null || true
```

### Q: 某个实验没有run.sh？
**A:** 部分简单实验可能需要手动运行，请参考该实验的README.md
