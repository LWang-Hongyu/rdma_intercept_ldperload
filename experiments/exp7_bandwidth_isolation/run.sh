#!/bin/bash
# EXP-7: 带宽隔离验证（简化版）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

# 网络配置
# 本端 (guolab-8): 管理IP用于SSH, RDMA IP用于测试
LOCAL_MGMT_IP="10.157.197.53"
LOCAL_RDMA_IP="192.168.108.2"
LOCAL_GID_INDEX="6"
# 对端 (guolab-6): 管理IP用于SSH, RDMA IP用于测试  
REMOTE_MGMT_IP="10.157.197.51"
REMOTE_RDMA_IP="192.168.106.2"
REMOTE_GID_INDEX="5"
DEV="mlx5_0"
GID_INDEX="6"
DURATION="15"
VICTIM_PORT="3001"
ATTACKER_PORT="4001"

echo "========================================"
echo "EXP-7: 带宽隔离验证"
echo "========================================"
echo ""
echo "网络配置:"
echo "  Local:  $LOCAL_RDMA_IP (guolab-8)"
echo "  Remote: $REMOTE_IP (guolab-6)"
echo ""

mkdir -p "$RESULTS_DIR"

# 检查拦截库
INTERCEPT_LIB="$PROJECT_DIR/build/librdma_intercept.so"
[ ! -f "$INTERCEPT_LIB" ] && INTERCEPT_LIB="/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so"

if [ ! -f "$INTERCEPT_LIB" ]; then
    echo "警告: 拦截库不存在"
fi

MODE=${1:-"help"}

if [ "$MODE" == "help" ]; then
    echo "用法:"
    echo "  $0 victim   - 运行Victim测试（需要配合远程Attacker）"
    echo "  $0 attacker - 在后台启动Attacker"
    echo ""
    exit 0
fi

# ========================================
# 场景1: 基线测试 - Victim单独运行
# ========================================
if [ "$MODE" == "baseline" ]; then
    echo "========================================"
    echo "场景1: Victim基线测试"
    echo "========================================"
    
    export LD_PRELOAD=$INTERCEPT_LIB
    export RDMA_INTERCEPT_ENABLE=1
    export RDMA_TENANT_ID="10"
    
    echo "启动Victim Server (端口 $VICTIM_PORT)..."
    ib_write_bw -d $DEV -x $LOCAL_GID_INDEX -s 1048576 -q 8 -D $DURATION --report_gbits -p $VICTIM_PORT &
    SERVER_PID=$!
    
    sleep 2
    
    echo "在远程启动Victim Client..."
    ssh why@$REMOTE_MGMT_IP "export LD_PRELOAD=$INTERCEPT_LIB; export RDMA_INTERCEPT_ENABLE=1; export RDMA_TENANT_ID='10'; ib_write_bw -d $DEV -x $REMOTE_GID_INDEX -s 1048576 -q 8 -D $DURATION --report_gbits -p $VICTIM_PORT $LOCAL_RDMA_IP" 2>&1 | tee "$RESULTS_DIR/exp7_baseline.log"
    
    wait $SERVER_PID 2>/dev/null || true
    
    # 提取带宽
    BW=$(grep "^[[:space:]]*[0-9]\+[[:space:]]\+[0-9]" "$RESULTS_DIR/exp7_baseline.log" | tail -1 | awk '{print $4}')
    echo $BW > "$RESULTS_DIR/exp7_baseline_bw.txt"
    echo ""
    echo "Victim基线带宽: ${BW} Gbps"
    
    exit 0
fi

# ========================================
# 场景2: 干扰测试
# ========================================
if [ "$MODE" == "interference" ]; then
    echo "========================================"
    echo "场景2: 干扰测试"
    echo "========================================"
    
    # 先确保远程启动了Attacker
    echo "请确保在远程已启动Attacker (./run.sh attacker)"
    echo "按回车继续..."
    read
    
    # 启动Victim Server
    export LD_PRELOAD=$INTERCEPT_LIB
    export RDMA_INTERCEPT_ENABLE=1
    export RDMA_TENANT_ID="10"
    
    echo "启动Victim Server (端口 $VICTIM_PORT)..."
    ib_write_bw -d $DEV -x $LOCAL_GID_INDEX -s 1048576 -q 8 -D $DURATION --report_gbits -p $VICTIM_PORT &
    SERVER_PID=$!
    
    sleep 2
    
    echo "在远程启动Victim Client（与Attacker并发）..."
    ssh why@$REMOTE_MGMT_IP "export LD_PRELOAD=$INTERCEPT_LIB; export RDMA_INTERCEPT_ENABLE=1; export RDMA_TENANT_ID='10'; ib_write_bw -d $DEV -x $LOCAL_GID_INDEX -s 1048576 -q 8 -D $DURATION --report_gbits -p $VICTIM_PORT $LOCAL_RDMA_IP" 2>&1 | tee "$RESULTS_DIR/exp7_interference.log"
    
    wait $SERVER_PID 2>/dev/null || true
    
    # 提取带宽
    BW=$(grep "^[[:space:]]*[0-9]\+[[:space:]]\+[0-9]" "$RESULTS_DIR/exp7_interference.log" | tail -1 | awk '{print $4}')
    echo $BW > "$RESULTS_DIR/exp7_interference_bw.txt"
    echo ""
    echo "Victim干扰带宽: ${BW} Gbps"
    
    # 计算隔离度
    if [ -f "$RESULTS_DIR/exp7_baseline_bw.txt" ]; then
        BASELINE=$(cat "$RESULTS_DIR/exp7_baseline_bw.txt")
        INTERFERENCE=$(cat "$RESULTS_DIR/exp7_interference_bw.txt")
        
        if [ -n "$BASELINE" ] && [ -n "$INTERFERENCE" ] && [ "$BASELINE" != "0" ]; then
            python3 << EOF
baseline = float("$BASELINE")
interference = float("$INTERFERENCE")
isolation = interference / baseline * 100
impact = (baseline - interference) / baseline * 100

print()
print("=" * 50)
print("EXP-7 带宽隔离测试结果")
print("=" * 50)
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
    print("状态: FAIL ✗ (隔离度 < 90%)")
print("=" * 50)
EOF
        fi
    fi
    
    exit 0
fi

# ========================================
# 启动Attacker（在后台）
# ========================================
if [ "$MODE" == "attacker" ]; then
    echo "启动Attacker（后台运行）..."
    
    export LD_PRELOAD=$INTERCEPT_LIB
    export RDMA_INTERCEPT_ENABLE=1
    export RDMA_TENANT_ID="20"
    
    # 启动Attacker Server
    ib_write_bw -d $DEV -x $LOCAL_GID_INDEX -s 1048576 -q 32 -D 60 --report_gbits -p $ATTACKER_PORT > /tmp/attacker_server.log 2>&1 &
    echo "Attacker Server PID: $!"
    
    sleep 2
    
    # 连接远程Client
    ssh why@$REMOTE_MGMT_IP "export LD_PRELOAD=$INTERCEPT_LIB; export RDMA_INTERCEPT_ENABLE=1; export RDMA_TENANT_ID='20'; ib_write_bw -d $DEV -x $REMOTE_GID_INDEX -s 1048576 -q 32 -D 60 --report_gbits -p $ATTACKER_PORT $LOCAL_RDMA_IP" > /tmp/attacker_client.log 2>&1 &
    
    echo "Attacker已启动，将在60秒后自动结束"
    exit 0
fi

echo "错误: 未知模式 '$MODE'"
echo "用法: $0 [baseline|interference|attacker|help]"
exit 1
