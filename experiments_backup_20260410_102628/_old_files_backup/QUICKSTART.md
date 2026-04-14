# EXP-MR-DEREG-V2: 快速开始清单

## 环境检查（执行一次）

```bash
# 1. 确认SSH免密登录
ssh why@10.157.197.51 "hostname"

# 2. 确认ib_write_bw可用
ib_write_bw --help | head -5
ssh why@10.157.197.51 "which ib_write_bw"

# 3. 确认Python3和matplotlib
python3 -c "import matplotlib; print('OK')"

# 4. 确认拦截库已编译
ls ~/rdma_intercept_ldpreload/build/librdma_intercept.so
```

## 快速实验（全自动）

```bash
cd ~/rdma_intercept_ldpreload/experiments
./run_exp_mr_dereg_v2.sh all
```

等待约60秒（每个场景30秒），查看结果。

## 查看结果

```bash
# 场景A（无拦截）结果
cat victim_no_protection.csv
# 或查看图表
# victim_no_protection.png

# 场景B（有拦截）结果
cat victim_with_protection.csv
# 或查看图表
# victim_with_protection.png
```

## 手动实验（分步）

如果自动脚本有问题，可以手动运行：

### 场景A：无拦截

**步骤1：对端启动服务器**
```bash
ssh why@10.157.197.51 "ib_write_bw -d mlx5_0 --report_gbits"
```

**步骤2：本机启动Victim监测器（终端1）**
```bash
cd ~/rdma_intercept_ldpreload/experiments
python3 victim_bw_monitor.py --mode=client --server=192.168.106.2 \
    --duration=30 --output=victim_no_protection.csv
```

**步骤3：5秒后启动Attacker（终端2）**
```bash
cd ~/rdma_intercept_ldpreload/experiments
./exp_mr_dereg_attacker --delay=5000 --duration=25000 --num_mrs=50 --batch_size=10
```

**步骤4：分析结果**
```bash
python3 analyze_bandwidth.py victim_no_protection.csv victim_no_protection.png
```

### 场景B：有拦截

**步骤1：启动守护进程（终端1）**
```bash
cd ~/rdma_intercept_ldpreload
sudo ./build/librdma_intercept_daemon --quota-mr=20
```

**步骤2：对端启动服务器**
```bash
ssh why@10.157.197.51 "ib_write_bw -d mlx5_0 --report_gbits"
```

**步骤3：使用LD_PRELOAD启动Victim（终端2）**
```bash
cd ~/rdma_intercept_ldpreload
LD_PRELOAD=./build/librdma_intercept.so \
    python3 experiments/victim_bw_monitor.py --mode=client --server=192.168.106.2 \
    --duration=30 --output=victim_with_protection.csv
```

**步骤4：使用LD_PRELOAD启动Attacker（终端3）**
```bash
cd ~/rdma_intercept_ldpreload
LD_PRELOAD=./build/librdma_intercept.so \
    ./experiments/exp_mr_dereg_attacker --delay=5000 --duration=25000 \
    --num_mrs=50 --batch_size=10
```

**步骤5：停止守护进程**
```bash
sudo pkill -f librdma_intercept_daemon
```

**步骤6：分析结果**
```bash
cd ~/rdma_intercept_ldpreload/experiments
python3 analyze_bandwidth.py victim_with_protection.csv victim_with_protection.png
```

## 预期结果

### 场景A（无拦截）
```
Baseline (0-5s):   ~80 Gbps
Attack phase:      ~35-50 Gbps
Degradation:       40-60%
Status:            ❌ VULNERABLE
```

### 场景B（有拦截）
```
Baseline (0-5s):   ~80 Gbps
Attack phase:      ~75-80 Gbps
Degradation:       0-10%
Status:            ✅ PROTECTED
```

## 故障排查

| 问题 | 解决方案 |
|------|---------|
| SSH失败 | 确认SSH密钥配置：`ssh-keygen && ssh-copy-id why@10.157.197.51` |
| ib_write_bw未找到 | `sudo apt-get install perftest` |
| matplotlib未找到 | `pip3 install matplotlib` |
| 攻击程序未编译 | `gcc -O2 -o exp_mr_dereg_attacker exp_mr_dereg_attacker.c -libverbs -lpthread` |
| 守护进程启动失败 | `sudo dmesg \| tail -20` 查看内核日志 |

## 清理残留进程

如果实验中断，可能需要清理：

```bash
# 本机清理
pkill -f exp_mr_dereg_attacker
pkill -f victim_bw_monitor
sudo pkill -f librdma_intercept_daemon

# 对端清理
ssh why@10.157.197.51 "pkill -f ib_write_bw"
```
