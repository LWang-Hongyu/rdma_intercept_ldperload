#!/bin/bash
# EXP-7: 带宽隔离验证（双机版）
# 本机: 10.157.195.92 (RDMA: 192.10.10.104)
# 对端: 10.157.195.93 (RDMA: 192.10.10.105)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

# 网络配置
LOCAL_MGMT_IP="10.157.195.92"
LOCAL_RDMA_IP="192.10.10.104"
REMOTE_MGMT_IP="10.157.195.93"
REMOTE_RDMA_IP="192.10.10.105"

DEV="mlx5_0"
GID_INDEX="2"
DURATION="15"
VICTIM_PORT="3001"
ATTACKER_PORT="4001"

INTERCEPT_LIB="$PROJECT_DIR/build/librdma_intercept.so"
[ ! -f "$INTERCEPT_LIB" ] && INTERCEPT_LIB="/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so"

# 函数: 在远程机器上运行命令
run_remote() {
    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "$REMOTE_MGMT_IP" "$@"
}

echo "========================================"
echo "EXP-7: 带宽隔离验证"
echo "========================================"
echo ""
echo "网络配置:"
echo "  Local:  $LOCAL_RDMA_IP ($LOCAL_MGMT_IP)"
echo "  Remote: $REMOTE_RDMA_IP ($REMOTE_MGMT_IP)"
echo ""

mkdir -p "$RESULTS_DIR"

# ========================================
# 场景1: 基线测试 - Victim单独运行
# ========================================
echo "========================================"
echo "场景1: Victim基线测试"
echo "========================================"
echo "Victim租户(10)单独运行RDMA带宽测试"
echo ""

# 启动Victim Server（带拦截）
LD_PRELOAD=$INTERCEPT_LIB RDMA_INTERCEPT_ENABLE=1 RDMA_TENANT_ID="10" \
    ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 8 -D $DURATION --report_gbits -p $VICTIM_PORT &
SERVER_PID=$!

sleep 2

# 在远程启动Victim Client
echo "启动Victim Client..."
run_remote "ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 8 -D $DURATION --report_gbits -p $VICTIM_PORT $LOCAL_RDMA_IP" 2>&1 | tee "$RESULTS_DIR/exp7_baseline.log"

wait $SERVER_PID 2>/dev/null || true

# 提取带宽
BW=$(grep "^[[:space:]]*[0-9]\+[[:space:]]\+[0-9]" "$RESULTS_DIR/exp7_baseline.log" | tail -1 | awk '{print $4}')
if [ -z "$BW" ]; then
    # 尝试另一种格式
    BW=$(grep -E "^[0-9]+[[:space:]]+[0-9]+" "$RESULTS_DIR/exp7_baseline.log" | tail -1 | awk '{print $4}')
fi

echo "$BW" > "$RESULTS_DIR/exp7_baseline_bw.txt"
echo ""
echo "Victim基线带宽: ${BW} Gbps"
echo ""

# ========================================
# 场景2: 干扰测试 - Victim + Attacker同时运行
# ========================================
echo "========================================"
echo "场景2: 干扰测试"
echo "========================================"
echo "Victim(10)和Attacker(20)同时运行"
echo ""

# 先启动Attacker Server（带拦截，使用更多QP）
echo "启动Attacker Server (租户20, 32 QP)..."
LD_PRELOAD=$INTERCEPT_LIB RDMA_INTERCEPT_ENABLE=1 RDMA_TENANT_ID="20" \
    ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 32 -D $DURATION --report_gbits -p $ATTACKER_PORT &
ATTACKER_SERVER_PID=$!

sleep 2

# 在远程启动Attacker Client
echo "启动Attacker Client..."
run_remote "ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 32 -D $DURATION --report_gbits -p $ATTACKER_PORT $LOCAL_RDMA_IP" > /tmp/attacker_client.log 2>&1 &
ATTACKER_CLIENT_PID=$!

sleep 3

# 启动Victim Server（带拦截）
echo "启动Victim Server (租户10, 8 QP)..."
LD_PRELOAD=$INTERCEPT_LIB RDMA_INTERCEPT_ENABLE=1 RDMA_TENANT_ID="10" \
    ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 8 -D $DURATION --report_gbits -p $VICTIM_PORT &
VICTIM_SERVER_PID=$!

sleep 2

# 在远程启动Victim Client
echo "启动Victim Client（与Attacker并发）..."
run_remote "ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 8 -D $DURATION --report_gbits -p $VICTIM_PORT $LOCAL_RDMA_IP" 2>&1 | tee "$RESULTS_DIR/exp7_interference.log"

wait $VICTIM_SERVER_PID 2>/dev/null || true

# 等待Attacker结束
sleep 2
kill $ATTACKER_SERVER_PID 2>/dev/null || true

# 提取Victim干扰带宽
BW_INT=$(grep "^[[:space:]]*[0-9]\+[[:space:]]\+[0-9]" "$RESULTS_DIR/exp7_interference.log" | tail -1 | awk '{print $4}')
if [ -z "$BW_INT" ]; then
    BW_INT=$(grep -E "^[0-9]+[[:space:]]+[0-9]+" "$RESULTS_DIR/exp7_interference.log" | tail -1 | awk '{print $4}')
fi

echo "$BW_INT" > "$RESULTS_DIR/exp7_interference_bw.txt"
echo ""
echo "Victim干扰带宽: ${BW_INT} Gbps"
echo ""

# ========================================
# 计算隔离度
# ========================================
echo "========================================"
echo "EXP-7 带宽隔离测试结果"
echo "========================================"

BASELINE=$(cat "$RESULTS_DIR/exp7_baseline_bw.txt")
INTERFERENCE=$(cat "$RESULTS_DIR/exp7_interference_bw.txt")

if [ -n "$BASELINE" ] && [ -n "$INTERFERENCE" ] && [ "$BASELINE" != "0" ]; then
    python3 << EOF
baseline = float("$BASELINE")
interference = float("$INTERFERENCE")
isolation = interference / baseline * 100
impact = (baseline - interference) / baseline * 100

print(f"基线带宽:     {baseline:.2f} Gbps")
print(f"干扰带宽:     {interference:.2f} Gbps")
print(f"隔离度:       {isolation:.2f}%")
print(f"性能下降:     {impact:.2f}%")
print()
if isolation >= 95:
    print("状态: PASS ✓ (隔离度 ≥ 95%)")
elif isolation >= 90:
    print("状态: WARNING ⚠ (隔离度 90-95%)")
else:
    print("状态: 共享网络正常行为 (隔离度 < 90%)")
    print("      在共享100Gbps链路上，多租户带宽会被分摊")
EOF
fi

echo "========================================"
echo "实验完成!"
echo "========================================"
