# EXP-MR-DEREG-V2: MR注销滥用攻击对Victim带宽影响实验

## 实验目标

验证MR注销滥用攻击（MR Deregistration Abuse）对Victim RDMA带宽的影响，以及拦截系统如何防御此类攻击。

**背景**: 根据NSDI'23论文，反复注销/注册MR会导致NIC的MTT（Memory Translation Table）缓存抖动，导致Victim的RDMA操作遭受缓存未命中，带宽下降40-60%。

## 网络拓扑

```
┌─────────────────┐                          ┌─────────────────┐
│   guolab-8      │      RDMA Network        │   guolab-6      │
│   (Attacker +   │◄────────────────────────►│   (Victim       │
│    Victim       │   mlx5_0, RoCEv2, 100G   │    Server)      │
│    Client)      │                          │                 │
├─────────────────┤                          ├─────────────────┤
│ mgmt:           │                          │ mgmt:           │
│ 10.157.197.53   │                          │ 10.157.197.51   │
│ RDMA:           │                          │ RDMA:           │
│ 192.168.108.2   │                          │ 192.168.106.2   │
└─────────────────┘                          └─────────────────┘
```

## 实验原理

### 攻击机制

```
Attacker的操作:
┌─────────────────────────────────────────────────────────────┐
│  Phase 1: 初始注册50个MR（每个4MB）                          │
│  for i = 1 to 50:                                           │
│      mr[i] = ibv_reg_mr(4MB)  ← 填充MTT缓存                 │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  Phase 2: 攻击循环（持续25秒）                               │
│  while (time < 25s):                                        │
│      // 注销10个MR                                          │
│      for i = 1 to 10:                                       │
│          ibv_dereg_mr(mr[i])  ← 触发MTT缓存失效             │
│      // 立即重新注册                                        │
│      for i = 1 to 10:                                       │
│          mr[i] = ibv_reg_mr(4MB)  ← 重新填充缓存            │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  对Victim的影响:                                             │
│  - NIC的MTT缓存被反复刷新                                    │
│  - Victim的MR查找产生缓存未命中                              │
│  - RDMA操作延迟增加，带宽下降40-60%                         │
└─────────────────────────────────────────────────────────────┘
```

### 防御机制

拦截系统通过MR配额限制防止攻击：
```
quota = 20  // 每个应用的MR配额

Attacker尝试注册50个MR:
  MR #1-20:  注册成功 ✓
  MR #21-50: 注册失败 ✗ (超出配额)

结果: Attacker只能注册20个MR，无法产生足够的MTT缓存压力
```

## 实验设计

### 时间线

```
时间(s) 0      5                       30
        │      │                        │
        ▼      ▼                        ▼
Victim  ┌───────────────────────────────────────┐
        │██████│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
        │基线  │      攻击阶段                  │
        │绿色  │        红色                    │
        └───────────────────────────────────────┘
                ┌─────────────────────────────┐
Attacker        │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
                │  MR注销/注册循环 (10个MR)    │
                └─────────────────────────────┘

图例:
██████  Phase 1 (0-5s):   Victim单独运行，建立带宽基线（绿色）
▓▓▓▓▓▓  Phase 2 (5-30s):  Attacker发动攻击，观测Victim带宽变化（红色）
```

### 预期结果对比

| 场景 | 配置 | 基线带宽 | 攻击阶段带宽 | 带宽下降 | 结论 |
|------|------|---------|-------------|---------|------|
| A | 无拦截 | ~80 Gbps | ~30-50 Gbps | 40-60% | ❌ 易受攻击 |
| B | 拦截系统 (quota=20) | ~80 Gbps | ~75-80 Gbps | 0-10% | ✅ 受到保护 |

## 快速开始

### 一键运行完整实验

```bash
cd ~/rdma_intercept_ldpreload/experiments
./run_exp_mr_dereg.sh all
```

这将自动运行两个场景并生成对比报告。

### 单独运行场景A（无拦截）

```bash
./run_exp_mr_dereg.sh baseline
```

### 单独运行场景B（有拦截）

```bash
./run_exp_mr_dereg.sh protected
```

## 详细手动步骤

### 前置条件检查

```bash
# 1. 确认SSH免密登录到对端
ssh why@10.157.197.51 "hostname"

# 2. 确认RDMA设备可用
ibstat mlx5_0
ssh why@10.157.197.51 "ibstat mlx5_0"

# 3. 确认RdmaEngine存在
ls -la ~/rdma_intercept_ldpreload/experiments/RdmaEngine
ssh why@10.157.197.51 "ls -la ~/rdma_intercept_ldpreload/experiments/RdmaEngine"

# 如果不存在，从Husky复制
scp ~/rdma-bench/test2-isolation/RdmaEngine \
    why@10.157.197.51:~/rdma_intercept_ldpreload/experiments/
```

### 场景A: 无拦截（攻击成功）

#### 步骤1: 在对端（guolab-6）启动Victim服务器

```bash
# 在对端（guolab-6）执行
ssh why@10.157.197.51
cd ~/rdma_intercept_ldpreload/experiments

# 启动带宽测试服务器
./RdmaEngine \
    --mode=server \
    --server_ip=192.168.106.2 \
    --server_port=20000 \
    --packet_size=4096 \
    --device_name=mlx5_0 \
    --duration=35 \
    --test_mode=write

# 预期输出:
# [INFO] Server mode, listening on 192.168.106.2:20000
# [INFO] Waiting for client connection...
```

#### 步骤2: 在本机（guolab-8）启动Victim客户端

```bash
# 在本机（guolab-8）执行
ssh why@10.157.197.53
cd ~/rdma_intercept_ldpreload/experiments

# 编译分析脚本（如果尚未编译）
gcc -O2 -o exp_mr_dereg_attacker exp_mr_dereg_attacker.c -libverbs -lpthread

# 启动Victim客户端（运行30秒，自动记录带宽）
./RdmaEngine \
    --mode=client \
    --server_ip=192.168.106.2 \
    --server_port=20000 \
    --packet_size=4096 \
    --device_name=mlx5_0 \
    --duration=30 \
    --test_mode=write \
    --output=victim_no_protection.csv

# 输出文件: victim_no_protection.csv
# 格式: timestamp,bandwidth_gbps,phase
```

#### 步骤3: 在本机启动Attacker（等待5秒后开始）

在Victim客户端启动后，**立即**在另一个终端启动Attacker：

```bash
# 在本机新开终端
ssh why@10.157.197.53
cd ~/rdma_intercept_ldpreload/experiments

# 启动Attacker（等待5秒后自动开始攻击）
./exp_mr_dereg_attacker \
    --delay=5000 \
    --duration=25000 \
    --num_mrs=50 \
    --batch_size=10

# 预期输出:
# ========================================
# MR Deregistration Abuse Attacker
# ========================================
# Delay:       5000 ms
# Duration:    25000 ms
# Num MRs:     50
# Batch size:  10
# MR size:     4 MB
# ========================================
# [Phase 0] Waiting 5000 ms before attack...
# [Phase 1] Registering 50 MRs...
# [Phase 1] Successfully registered 50/50 MRs
# [Phase 2] Starting attack loop (deregister/reregister 10 MRs)...
# [Phase 2] Running for 25000 ms...
# [Phase 2] Cycles: 10000, Elapsed: 5000 ms
# ...
# [Phase 2] Attack completed. Total cycles: 50000
```

#### 步骤4: 分析结果

```bash
# Victim客户端结束后（约30秒），分析结果
python3 analyze_bandwidth.py \
    victim_no_protection.csv \
    victim_no_protection.png

# 预期输出:
# ========================================
# Results: victim_no_protection.csv
# ========================================
# Samples:          300 points
# Baseline (0-5s):  82.3 ± 3.2 Gbps (n=50)
# Attack phase:     38.7 ± 8.5 Gbps (n=250)
# Degradation:      53.0%
# Status:           ❌ VULNERABLE (attack effective)
# ========================================
#
# Plot saved to: victim_no_protection.png
```

### 场景B: 有拦截（攻击失败）

#### 步骤1: 启动拦截守护进程（本机）

```bash
# 在本机执行
ssh why@10.157.197.53
cd ~/rdma_intercept_ldpreload

# 启动守护进程，设置MR配额为20
sudo ./build/librdma_intercept_daemon \
    --quota-mr=20 \
    --config=./config/mr_limit_20.json

# 预期输出:
# [INFO] RDMA Intercept Daemon started
# [INFO] MR quota: 20 per process
# ...
```

#### 步骤2: 在对端启动Victim服务器

```bash
# 在对端执行（同场景A步骤1）
ssh why@10.157.197.51
cd ~/rdma_intercept_ldpreload/experiments

./RdmaEngine \
    --mode=server \
    --server_ip=192.168.106.2 \
    --server_port=20000 \
    --packet_size=4096 \
    --device_name=mlx5_0 \
    --duration=35 \
    --test_mode=write
```

#### 步骤3: 使用LD_PRELOAD启动Victim客户端

```bash
# 在本机执行
ssh why@10.157.197.53
cd ~/rdma_intercept_ldpreload

# 使用LD_PRELOAD加载拦截库
LD_PRELOAD=./build/librdma_intercept.so \
    ./experiments/RdmaEngine \
    --mode=client \
    --server_ip=192.168.106.2 \
    --server_port=20000 \
    --packet_size=4096 \
    --device_name=mlx5_0 \
    --duration=30 \
    --test_mode=write \
    --output=victim_with_protection.csv

# 注意：Victim客户端也会被配额限制，但正常应用不会注册过多MR
```

#### 步骤4: 使用LD_PRELOAD启动Attacker

```bash
# 在本机新开终端
ssh why@10.157.197.53
cd ~/rdma_intercept_ldpreload

# Attacker也使用LD_PRELOAD，会被quota=20限制
LD_PRELOAD=./build/librdma_intercept.so \
    ./experiments/exp_mr_dereg_attacker \
    --delay=5000 \
    --duration=25000 \
    --num_mrs=50 \
    --batch_size=10

# 预期输出:
# [Phase 1] Registering 50 MRs...
# [WARN] Failed to register MR 21 (errno=28)  ← 配额限制！
# [Phase 1] Successfully registered 20/50 MRs  ← 只能注册20个
# ...
```

#### 步骤5: 分析结果

```bash
python3 experiments/analyze_bandwidth.py \
    experiments/victim_with_protection.csv \
    experiments/victim_with_protection.png

# 预期输出:
# Baseline (0-5s):  79.8 ± 2.9 Gbps (n=50)
# Attack phase:     77.2 ± 4.1 Gbps (n=250)
# Degradation:      3.3%
# Status:           ✅ PROTECTED (attack ineffective)
```

## 结果解读

### 成功标准

| 指标 | 场景A（无拦截） | 场景B（有拦截） |
|------|----------------|----------------|
| 基线带宽 | ~80 Gbps | ~80 Gbps |
| 攻击阶段带宽 | 35-50 Gbps (-40% to -60%) | 75-80 Gbps (-5% to -10%) |
| Attacker注册MR数 | 50 | 20 |
| 保护效果 | ❌ 攻击有效 | ✅ 攻击被阻止 |

### 可视化图表说明

生成的PNG图表包含：
- **绿色点**: Phase 1（0-5s）基线测量数据
- **红色点**: Phase 2（5-30s）攻击阶段数据
- **蓝色虚线**: 基线平均带宽
- **红色虚线**: 攻击阶段平均带宽
- **灰色竖线**: 攻击开始时间点（5s）

### 失败排查

#### 问题1: Victim带宽始终很低
```bash
# 检查RDMA网络连通性
ib_write_bw -d mlx5_0 &
ib_write_bw -d mlx5_0 192.168.106.2

# 正常应该显示 >50Gbps
```

#### 问题2: 攻击无效
```bash
# 检查Attacker是否在运行
ps aux | grep exp_mr_dereg_attacker

# 检查攻击循环次数（应在日志中显示数万次）
tail /tmp/attacker.log
```

#### 问题3: 拦截不生效
```bash
# 检查守护进程
ps aux | grep librdma_intercept_daemon

# 检查LD_PRELOAD是否设置正确
echo $LD_PRELOAD

# 检查配额配置
sudo cat /var/log/intercept_daemon.log | grep quota
```

## 自动化脚本详情

### 脚本参数

```bash
./run_exp_mr_dereg.sh [SCENARIO]

SCENARIO选项:
  baseline   - 仅运行场景A（无拦截）
  protected  - 仅运行场景B（有拦截）
  all        - 运行两个场景（默认）
```

### 输出文件

```
experiments/
├── victim_no_protection.csv    # 场景A原始数据
├── victim_no_protection.png    # 场景A可视化图表
├── victim_with_protection.csv  # 场景B原始数据
└── victim_with_protection.png  # 场景B可视化图表
```

### 日志文件

```
/tmp/
├── remote_server.log      # 远端Victim服务器日志
├── victim_client.log      # 本机Victim客户端日志
├── attacker.log           # Attacker日志
└── daemon.log             # 拦截守护进程日志（场景B）
```

## 参考文献

实验设计基于以下论文的MR deregistration abuse attack：

```
Understanding RDMA Microarchitecture Resources for Performance Isolation
Xinhao Kong, Jingrong Chen, Wei Bai, et al.
NSDI 2023
```

关键发现：
- MR注销滥用会导致MTT缓存抖动
- Victim带宽下降40-60%
- 需要限制MR数量来防止攻击

## 附录: 实验脚本清单

| 文件 | 说明 |
|------|------|
| `exp_mr_dereg_attacker.c` | 攻击程序源码 |
| `exp_mr_dereg_attacker` | 攻击程序（编译后） |
| `analyze_bandwidth.py` | 结果分析与可视化脚本 |
| `run_exp_mr_dereg.sh` | 自动化实验脚本 |
| `EXP-MR-DEREG-V2.md` | 本文档 |
