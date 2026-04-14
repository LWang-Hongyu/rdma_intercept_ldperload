# Paper Results 目录

本目录存放所有实验的原始数据和生成的图表。每个实验有独立的子目录，包含数据文件和画图脚本。

## 目录结构

```
paper_results/
├── README.md                 # 本文件
├──
├── exp1/                     # EXP-1: 微基准测试
│   ├── baseline.txt          # 基线数据
│   ├── baseline_v2.txt       # 基线数据v2（排除冷启动）
│   ├── intercept.txt         # 拦截后数据
│   ├── intercepted_v2.txt    # 拦截后数据v2
│   ├── plot.py               # ⭐ 画图脚本（可独立运行）
│   ├── exp1_microbenchmark_v2.png  # 生成的图表
│   ├── exp1_microbenchmark_v2.pdf
│   └── RESULT_SUMMARY.md     # 结果摘要
│
├── exp2/                     # EXP-2: 多租户隔离
│   ├── scene1_single.txt     # 场景1数据
│   ├── scene2_tenantA.txt    # 场景2数据（租户A）
│   ├── scene2_tenantB.txt    # 场景2数据（租户B）
│   ├── scene3_*.txt          # 场景3数据
│   ├── plot.py               # ⭐ 画图脚本
│   ├── exp2_multi_tenant_isolation.png
│   ├── exp2_multi_tenant_isolation.pdf
│   └── RESULT_SUMMARY.md
│
├── exp5/                     # EXP-5: 动态策略
│   ├── dynamic_policy_effect.txt
│   ├── plot.py               # ⭐ 画图脚本
│   ├── exp5_hot_update_effect.png
│   ├── exp5_hot_update_effect.pdf
│   └── RESULT_SUMMARY.md
│
├── exp8/                     # EXP-8: QP隔离
│   ├── result.txt
│   ├── plot.py               # ⭐ 画图脚本
│   ├── exp8_qp_isolation.png
│   ├── exp8_qp_isolation.pdf
│   └── RESULT_SUMMARY.md
│
├── exp9/                     # EXP-9: MR隔离
│   ├── result.txt
│   ├── plot.py               # ⭐ 画图脚本
│   ├── exp9_mr_isolation.png
│   ├── exp9_mr_isolation.pdf
│   └── RESULT_SUMMARY.md
│
└── exp_mr_dereg/             # EXP-MR-DEREG: MR注销滥用攻击
    ├── plot_mr_dereg_impact.py    # ⭐ 画图脚本
    ├── plot_mr_dereg_motivation.py
    ├── fig_mr_dereg_impact.png    # 生成的图表
    ├── fig_mr_dereg_impact.pdf
    └── ...
```

## 使用方法

### 生成图表（推荐方式）

进入任意实验目录，直接运行画图脚本：

```bash
# 示例：生成EXP-1图表
cd exp1
python3 plot.py
# 输出：exp1_microbenchmark_v2.png, exp1_microbenchmark_v2.pdf

# 示例：生成EXP-2图表
cd ../exp2
python3 plot.py
# 输出：exp2_multi_tenant_isolation.png, exp2_multi_tenant_isolation.pdf
```

**特点**：
- ✅ 画图脚本使用**相对路径**读取本地数据
- ✅ 生成的图表保存在**当前目录**
- ✅ 无需修改代码，即开即用

### 查看原始数据

数据文件是文本格式，可以直接查看：

```bash
# EXP-1
head exp1/baseline_v2.txt

# EXP-2  
cat exp2/scene1_single.txt

# EXP-8/9
cat exp8/result.txt
```

## 数据文件格式

### EXP-1 数据格式

```
# EXP-1 v2: 微基准测试结果 (排除冷启动)
## NORMAL_QP_CREATE_LATENCY (us)
MEAN: 464.331
P50: 457.893
P95: 496.879
P99: 729.635
## MR_REG_LATENCY (us)
MEAN: 22.307
```

### EXP-2 数据格式

```
# EXP-2: Multi-Tenant Isolation Test
TENANT_ID: 10
QUOTA: 50
ATTEMPTS: 60
CREATED: 50
DENIED: 10
QUOTA_COMPLIANCE: 100.0%
FIRST_DENIAL_MS: 49.49

# Raw latency data
0,26115.48,1
1,525.12,1
```

### EXP-8/9 数据格式

```
Attempt: 20
Successfully created: 10 QP(s)
Failed: 10 QP(s)
Quota enforcement: 100.00%
```

## 与 experiments 目录的关系

```
experiments/              paper_results/
    │                          │
    ├── exp1/  ───────────────→├── exp1/
    │   ├── src/                   ├── baseline.txt
    │   ├── run.sh                 ├── plot.py
    │   └── results/               └── *.png
    │
    ├── exp2/  ───────────────→├── exp2/
    │   └── ...                    └── ...
```

- `experiments/expX/` - 实验代码和运行脚本
- `paper_results/expX/` - 实验数据和可视化图表

## 图表清单

| 实验 | 图表文件 | 说明 |
|------|---------|------|
| EXP-1 | `exp1_microbenchmark_v2.png` | 微基准测试：操作延迟对比 |
| EXP-2 | `exp2_multi_tenant_isolation.png` | 多租户隔离：配额合规性和公平性 |
| EXP-5 | `exp5_hot_update_effect.png` | 动态策略：热更新延迟 |
| EXP-8 | `exp8_qp_isolation.png` | QP数量隔离测试 |
| EXP-9 | `exp9_mr_isolation.png` | MR数量隔离测试 |
| EXP-MR-DEREG | `fig_mr_dereg_impact.png` | MR注销滥用攻击影响 |

## 添加新的实验数据

1. 创建实验目录：`mkdir exp_new`
2. 放入数据文件：`cp data.txt exp_new/`
3. 创建画图脚本：`vim exp_new/plot.py`
   - 使用相对路径读取数据（如 `open('data.txt')`）
   - 保存图表到当前目录（如 `plt.savefig('chart.png')`）
4. 运行：`cd exp_new && python3 plot.py`
