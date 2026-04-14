# EXP-7: 带宽隔离验证实验

**结果位置**: 本实验的结果保存在 `results/` 和 `analysis/` 目录下
- `results/exp7_baseline_bw.txt` - Victim基线带宽数据
- `results/exp7_interference_bw.txt` - 干扰测试带宽数据
- `results/exp7_summary.txt` - 实验汇总报告
- `analysis/exp7_bandwidth_isolation.png` - 带宽隔离可视化图表

## 实验目标

验证多租户场景下的带宽隔离效果，评估一个租户的高带宽传输对另一租户的影响程度。

**核心问题**: 在共享RDMA网络中，Attacker租户的高带宽传输是否会严重影响Victim租户的性能？软件层面的隔离能否实现有效的带宽保护？

---

## 实验设置

### 硬件环境

| 组件 | 配置 |
|------|------|
| **服务器型号** | 物理服务器 (2台) |
| **CPU** | Intel Xeon处理器（多核） |
| **内存** | 128GB+ DDR4 |
| **RDMA网卡** | NVIDIA BlueField-3 (rev:1) |
| **网卡端口** | 双端口 (mlx5_0, mlx5_1) |
| **PCIe位置** | 42:00.0, 42:00.1 |
| **网络带宽** | 100 Gbps RoCEv2 |
| **操作系统** | Ubuntu 20.04 LTS |

### 网络拓扑

```
┌─────────────────────────────────────────────────────────────────────┐
│                    双机带宽隔离测试拓扑                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│   ┌─────────────────────┐         ┌─────────────────────┐          │
│   │    Server (本机)     │◄───────►│    Client (对端)     │          │
│   │    guolab-8         │  RoCE   │    guolab-4         │          │
│   │    192.10.10.104    │ 100Gbps │    192.10.10.105    │          │
│   │                     │         │                     │          │
│   │  ┌───────────────┐  │         │  ┌───────────────┐  │          │
│   │  │  Victim租户   │  │         │  │  Victim租户   │  │          │
│   │  │  - 租户ID: 10 │  │         │  │  - 租户ID: 10 │  │          │
│   │  │  - QP数: 8    │  │         │  │  - QP数: 8    │  │          │
│   │  │  - 端口: 3001 │  │         │  │  - 端口: 3001 │  │          │
│   │  └───────┬───────┘  │         │  └───────┬───────┘  │          │
│   │          │          │         │          │          │          │
│   │  ┌───────┴───────┐  │         │  ┌───────┴───────┐  │          │
│   │  │ Attacker租户  │  │         │  │ Attacker租户  │  │          │
│   │  │  - 租户ID: 20 │  │         │  │  - 租户ID: 20 │  │          │
│   │  │  - QP数: 32   │  │         │  │  - QP数: 32   │  │          │
│   │  │  - 端口: 4001 │  │         │  │  - 端口: 4001 │  │          │
│   │  └───────┬───────┘  │         │  └───────┬───────┘  │          │
│   │          │          │         │          │          │          │
│   │          ▼          │         │          ▼          │          │
│   │  ┌───────────────┐  │         │  ┌───────────────┐  │          │
│   │  │  BlueField-3  │◄─┘         └─►│  BlueField-3  │  │          │
│   │  │   mlx5_0      │    100Gbps     │   mlx5_0      │  │          │
│   │  └───────────────┘                └───────────────┘  │          │
│   │                                                      │          │
│   │  管理IP: 10.157.195.92          管理IP: 10.157.195.93 │          │
│   └─────────────────────┘         └─────────────────────┘          │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 软件环境

| 组件 | 版本/配置 |
|------|----------|
| **测试工具** | perftest (ib_write_bw) |
| **拦截库** | librdma_intercept.so |
| **设备** | mlx5_0 |
| **GID Index** | 2 |
| **测试时长** | 15秒 |

### 租户配置

| 租户 | ID | QP数量 | 角色 | 目的 |
|------|-----|--------|------|------|
| **Victim** | 10 | 8 | 被测试租户 | 测量在干扰下的带宽 |
| **Attacker** | 20 | 32 | 干扰租户 | 产生高带宽竞争 |

---

## 实验方法

### 测试设计

**对比测试:**

| 场景 | 配置 | 目的 |
|------|------|------|
| **基线测试** | 仅Victim运行 | 测量Victim单独运行时的最大带宽 |
| **干扰测试** | Victim + Attacker同时运行 | 测量Victim在干扰下的带宽 |

**测试变量:**

| 参数 | Victim | Attacker |
|------|--------|----------|
| **租户ID** | 10 | 20 |
| **QP数量** | 8 | 32 |
| **消息大小** | 1MB | 1MB |
| **端口** | 3001 | 4001 |

### 测试流程

```
实验流程:

1. 基线测试 (Victim单独运行)
   ┌─────────────────────────────────────────────────────────────┐
   │  Server端 (本机)              Client端 (对端)               │
   │  Victim(10) 8 QP              Victim(10) 8 QP               │
   │  LD_PRELOAD=...               LD_PRELOAD=...                │
   │  ib_write_bw -p 3001          ib_write_bw -p 3001           │
   │              -q 8                          -q 8              │
   │              -s 1MB                        -s 1MB            │
   │              -D 15                         -D 15             │
   │              192.10.10.104                                  │
   └─────────────────────────────────────────────────────────────┘
   测量结果: 91.51 Gbps (基线带宽)

2. 干扰测试 (Victim + Attacker同时运行)
   ┌─────────────────────────────────────────────────────────────┐
   │  Server端 (本机)              Client端 (对端)               │
   │  ┌─────────────┐              ┌─────────────┐               │
   │  │ Victim(10)  │              │ Victim(10)  │               │
   │  │ 8 QP        │              │ 8 QP        │               │
   │  │ -p 3001     │              │ -p 3001     │               │
   │  └──────┬──────┘              └──────┬──────┘               │
   │         │                            │                       │
   │  ┌──────┴──────┐              ┌──────┴──────┐               │
   │  │ Attacker(20)│              │ Attacker(20)│               │
   │  │ 32 QP       │              │ 32 QP       │               │
   │  │ -p 4001     │              │ -p 4001     │               │
   │  └─────────────┘              └─────────────┘               │
   └─────────────────────────────────────────────────────────────┘
   测量结果: Victim = 16.51 Gbps, Attacker = ~75 Gbps
```

### 关键指标

**隔离度计算公式:**
```
隔离度 = (干扰带宽 / 基线带宽) × 100%

例如:
- 基线带宽: 91.51 Gbps (Victim单独)
- 干扰带宽: 16.51 Gbps (Victim+Attacker)
- 隔离度 = 16.51/91.51 × 100% = 18.04%
```

| 指标 | 定义 | 目标值 |
|------|------|--------|
| **隔离度** | 干扰带宽/基线带宽 | ≥95% (理想) |
| **性能下降** | (基线-干扰)/基线 | <5% (理想) |

---

## 实验结果

### 原始数据

**基线测试（Victim单独运行）:**
```
Victim基线带宽: 91.51 Gbps
```

**干扰测试（Victim + Attacker同时运行）:**
```
Victim干扰带宽: 16.51 Gbps
Attacker带宽: ~75 Gbps (估算)
总带宽: ~91.51 Gbps (接近网卡上限)
```

### 性能对比

| 场景 | Victim带宽(Gbps) | 占总带宽比例 | 隔离度 |
|------|-----------------|-------------|--------|
| **基线** | 91.51 | 100% | 100% |
| **干扰** | 16.51 | 18% | 18.04% |

### 带宽分配分析

在100Gbps共享链路上，两个租户同时传输时的带宽分配：

```
总带宽: 100 Gbps
├─ Victim (8 QP):  16.51 Gbps (18%)
└─ Attacker (32 QP): ~75 Gbps (82%)

QP比例: Victim:Attacker = 8:32 = 1:4
带宽比例: Victim:Attacker ≈ 1:4.5
```

### 关键发现

**1. 带宽共享而非隔离**
- 在共享网络中，带宽被两个租户分摊
- Victim从91.51 Gbps下降到16.51 Gbps（下降82%）
- 隔离度仅18.04%，远低于95%目标

**2. QP数量影响带宽分配**
- Attacker使用32 QP，Victim使用8 QP
- QP比例1:4，带宽比例约1:4.5
- 更多QP获得更多带宽

**3. 软件层面隔离能力有限**
- 纯软件拦截无法控制底层网络带宽分配
- 需要硬件QoS或SR-IOV才能实现严格隔离

---

## 实验结论

### 主要发现

1. **共享网络中完全带宽隔离难以实现**
   - 软件层面拦截无法控制物理带宽分配
   - 在100Gbps共享链路上，多个租户竞争带宽
   - 隔离度仅18%，远低于95%设计目标

2. **系统实现了公平共享**
   - 带宽按QP数量比例分配
   - Victim(8 QP)获得约18%带宽
   - Attacker(32 QP)获得约82%带宽
   - 比例大致符合QP数量比(1:4)

3. **软件隔离的局限性**
   - LD_PRELOAD拦截仅在控制面生效
   - 数据面传输直接通过网卡，软件无法控制
   - 需要硬件支持才能实现严格带宽隔离

### 实际意义

本实验揭示了软件层面带宽隔离的挑战：
- ⚠️ **软件隔离有限**: 纯软件方案无法实现严格带宽隔离
- ✅ **公平共享**: 系统能够保证基于QP数量的公平分配
- 🔧 **改进方向**: 需要硬件QoS、SR-IOV或网络虚拟化技术

### 与预期的对比

| 指标 | 预期 | 实际 | 评价 |
|------|------|------|------|
| 隔离度 | ≥95% | 18% | ❌ 未达标 |
| 公平性 | 按QP分配 | 基本符合 | ⚠️ 部分达标 |
| 总带宽 | 稳定 | 稳定 | ✅ 达标 |

### 改进建议

1. **硬件QoS**: 使用网卡的硬件QoS功能限制每个租户的最大带宽
2. **SR-IOV**: 为每个租户分配独立的虚拟功能(VF)
3. **网络虚拟化**: 使用VXLAN等Overlay网络隔离租户流量
4. **应用层限流**: 在应用层实现发送速率控制

---

## 快速开始

### 一键运行实验

```bash
cd /home/why/rdma_intercept_ldpreload/experiments/exp7_bandwidth_isolation

# 运行完整实验（双机测试）
bash run_exp7.sh
```

### 手动测试

**Step 1: 在Server端(本机)启动服务**

```bash
# 基线测试 - 仅Victim
export LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so
export RDMA_INTERCEPT_ENABLE=1
export RDMA_TENANT_ID="10"
ib_write_bw -d mlx5_0 -x 2 -s 1048576 -q 8 -D 15 --report_gbits -p 3001

# 干扰测试 - Victim + Attacker
# 终端1: Victim Server
export LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so
export RDMA_INTERCEPT_ENABLE=1
export RDMA_TENANT_ID="10"
ib_write_bw -d mlx5_0 -x 2 -s 1048576 -q 8 -D 15 --report_gbits -p 3001 &

# 终端2: Attacker Server
export LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so
export RDMA_INTERCEPT_ENABLE=1
export RDMA_TENANT_ID="20"
ib_write_bw -d mlx5_0 -x 2 -s 1048576 -q 32 -D 15 --report_gbits -p 4001
```

**Step 2: 在Client端(对端)启动客户端**

```bash
# Victim Client
ib_write_bw -d mlx5_0 -x 2 -s 1048576 -q 8 -D 15 --report_gbits -p 3001 192.10.10.104

# Attacker Client
ib_write_bw -d mlx5_0 -x 2 -s 1048576 -q 32 -D 15 --report_gbits -p 4001 192.10.10.104
```

### 查看结果

```bash
# 查看基线带宽
cat results/exp7_baseline_bw.txt

# 查看干扰带宽
cat results/exp7_interference_bw.txt

# 查看汇总
cat results/exp7_summary.txt

# 查看图表
eog analysis/exp7_bandwidth_isolation.png
```

---

## 文件清单

| 文件/目录 | 说明 |
|----------|------|
| `run_exp7.sh` | 双机测试脚本 |
| `run.sh` | 基础运行脚本 |
| `run_local.sh` | 单机测试脚本 |
| `src/` | 源代码目录 |
| `results/exp7_baseline_bw.txt` | Victim基线带宽 |
| `results/exp7_interference_bw.txt` | 干扰测试带宽 |
| `results/exp7_summary.txt` | 实验汇总报告 |
| `analysis/plot.py` | 绘图脚本 |
| `analysis/exp7_bandwidth_isolation.png` | 结果可视化图表 |

---

## 注意事项

1. **双机配合**: 本实验需要两台机器同时配合执行
2. **网络配置**: 确保RDMA网络互通（RoCEv2，GID Index 2）
3. **租户创建**: 实验前需创建租户10和租户20
4. **同步启动**: 确保Victim和Attacker客户端同时开始测试
5. **硬件限制**: 软件层面无法实现严格带宽隔离，这是预期行为

### 创建租户

```bash
# 在Server端创建租户
tenant_manager_client create 10 100 100 "Victim"
tenant_manager_client create 20 100 100 "Attacker"
```
