#!/bin/bash
# EXP-MR-DEREG: 手动模式实验脚本
# 
# 使用方式:
# 1. 在远端 (guolab-6) 手动启动服务器 (server 模式 - 不指定IP):
#    ssh why@10.157.197.51
#    ib_send_bw -F -R --report_gbits --run_infinitely -d mlx5_0
#
# 2. 在本端 (guolab-8) 运行此脚本 (client 模式 - 连接远端):
#    ./run_manual.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

LOCAL_MGMT="10.157.197.53"
REMOTE_MGMT="10.157.197.51"
REMOTE_RDMA="192.168.106.2"

echo "========================================"
echo "EXP-MR-DEREG: MR Deregistration Abuse"
echo "========================================"
echo ""
echo "网络配置:"
echo "  本端: $LOCAL_MGMT (RDMA: 192.168.108.2)"
echo "  远端: $REMOTE_MGMT (RDMA: $REMOTE_RDMA)"
echo ""
echo "请先确保远端服务器已启动:"
echo "  ssh why@$REMOTE_MGMT"
echo "  ib_send_bw -F -R --report_gbits --run_infinitely -d mlx5_0"
echo ""
echo "注意: 远端不需要指定IP，等待连接即可"
echo ""
read -p "确认远端服务器已启动，按 Enter 继续..."

echo ""
echo "========================================"
echo "场景 A: 无拦截 (基线测试)"
echo "========================================"

mkdir -p "$RESULTS_DIR"

# 启动 Victim 监测 (client 模式，连接远端)
echo "[INFO] 启动 Victim 带宽监测 (30秒)..."
echo "       命令: ib_send_bw -F -R --report_gbits -d mlx5_0 $REMOTE_RDMA"
python3 "$SCRIPT_DIR/src/victim_bw_monitor.py" \
    --mode=client \
    --server=$REMOTE_RDMA \
    --duration=30 \
    --output="$RESULTS_DIR/victim_no_protection.csv" &
VICTIM_PID=$!

sleep 5

if ! ps -p $VICTIM_PID > /dev/null 2>&1; then
    echo "[ERROR] Victim 监测器启动失败"
    echo "       请检查远端服务器是否正常运行"
    exit 1
fi

echo "[INFO] Victim 已连接，启动 Attacker..."
"$SCRIPT_DIR/attacker" --delay=0 --duration=25000 --num_mrs=50 --batch_size=10 &
ATTACKER_PID=$!

echo "[INFO] 实验运行中 (30秒)..."
for i in {1..30}; do
    sleep 1
    if [ $((i % 5)) -eq 0 ]; then
        echo "  进度: ${i}/30 秒"
    fi
done

wait $VICTIM_PID 2>/dev/null || true
kill $ATTACKER_PID 2>/dev/null || true

echo ""
echo "========================================"
echo "场景 A 完成"
echo "========================================"
echo "结果: $RESULTS_DIR/victim_no_protection.csv"
echo ""
read -p "按 Enter 继续到场景 B (有拦截)..."

echo ""
echo "========================================"
echo "场景 B: 有拦截 (quota=20)"
echo "========================================"

# 启动守护进程
echo "[INFO] 启动拦截守护进程..."
sudo "$PROJECT_DIR/build/librdma_intercept_daemon" --quota-mr=20 &
DAEMON_PID=$!
sleep 2

# 使用 LD_PRELOAD 启动 Victim (client 模式)
echo "[INFO] 启动 Victim (带拦截)..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" \
    python3 "$SCRIPT_DIR/src/victim_bw_monitor.py" \
    --mode=client \
    --server=$REMOTE_RDMA \
    --duration=30 \
    --output="$RESULTS_DIR/victim_with_protection.csv" &
VICTIM_PID=$!

sleep 5

if ! ps -p $VICTIM_PID > /dev/null 2>&1; then
    echo "[ERROR] Victim 启动失败"
    sudo kill $DAEMON_PID 2>/dev/null || true
    exit 1
fi

echo "[INFO] 启动 Attacker (带拦截)..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" \
    "$SCRIPT_DIR/attacker" --delay=0 --duration=25000 --num_mrs=50 --batch_size=10 &
ATTACKER_PID=$!

echo "[INFO] 实验运行中 (30秒)..."
for i in {1..30}; do
    sleep 1
    if [ $((i % 5)) -eq 0 ]; then
        echo "  进度: ${i}/30 秒"
    fi
done

wait $VICTIM_PID 2>/dev/null || true
kill $ATTACKER_PID 2>/dev/null || true
sudo kill $DAEMON_PID 2>/dev/null || true

echo ""
echo "========================================"
echo "实验完成"
echo "========================================"
echo "结果文件:"
echo "  $RESULTS_DIR/victim_no_protection.csv"
echo "  $RESULTS_DIR/victim_with_protection.csv"
echo ""
if [ -f "$RESULTS_DIR/victim_no_protection.csv" ]; then
    echo "生成图表:"
    python3 "$SCRIPT_DIR/analysis/analyze_bandwidth.py" "$RESULTS_DIR/victim_no_protection.csv" "$RESULTS_DIR/victim_no_protection.png" 2>/dev/null || true
    python3 "$SCRIPT_DIR/analysis/analyze_bandwidth.py" "$RESULTS_DIR/victim_with_protection.csv" "$RESULTS_DIR/victim_with_protection.png" 2>/dev/null || true
    echo "  $RESULTS_DIR/victim_no_protection.png"
    echo "  $RESULTS_DIR/victim_with_protection.png"
fi
