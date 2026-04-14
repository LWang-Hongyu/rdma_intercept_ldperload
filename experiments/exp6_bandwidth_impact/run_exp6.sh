#!/bin/bash
# EXP-6: RDMA数据面带宽影响测试（双机版）
# 本机: 10.157.195.92 (RDMA: 192.10.10.104)
# 对端: 10.157.195.93 (RDMA: 192.10.10.105)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

# 网络配置 - 使用用户提供的IP
LOCAL_MGMT_IP="10.157.195.92"
LOCAL_RDMA_IP="192.10.10.104"
REMOTE_MGMT_IP="10.157.195.93"
REMOTE_RDMA_IP="192.10.10.105"

DEV="mlx5_0"
GID_INDEX="6"
DURATION="10"
ITERATIONS=3

# 消息大小列表
MSG_SIZES=(65536 262144 1048576)  # 64KB, 256KB, 1MB

echo "========================================"
echo "EXP-6: RDMA数据面带宽影响测试（双机版）"
echo "========================================"
echo ""
echo "网络配置:"
echo "  Local:  $LOCAL_RDMA_IP ($LOCAL_MGMT_IP)"
echo "  Remote: $REMOTE_RDMA_IP ($REMOTE_MGMT_IP)"
echo ""

# 检查依赖
if ! command -v ib_write_bw &> /dev/null; then
    echo "错误: 未找到ib_write_bw"
    exit 1
fi

mkdir -p "$RESULTS_DIR"

INTERCEPT_LIB="$PROJECT_DIR/build/librdma_intercept.so"
[ ! -f "$INTERCEPT_LIB" ] && INTERCEPT_LIB="/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so"

BASELINE_CSV="$RESULTS_DIR/exp6_baseline.csv"
INTERCEPT_CSV="$RESULTS_DIR/exp6_intercept.csv"

# 初始化CSV文件
echo "msg_size,bw_gbps,msg_rate" > "$BASELINE_CSV"
echo "msg_size,bw_gbps,msg_rate" > "$INTERCEPT_CSV"

# 函数: 在远程机器上运行命令
run_remote() {
    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "$REMOTE_MGMT_IP" "$@"
}

# 函数: 运行带宽测试
run_bw_test() {
    local msg_size=$1
    local use_intercept=$2
    local csv_file=$3
    local iteration=$4
    
    echo "  消息大小: $msg_size bytes (第$iteration轮)"
    
    # 启动本地server
    if [ "$use_intercept" -eq 1 ]; then
        LD_PRELOAD="$INTERCEPT_LIB" RDMA_INTERCEPT_ENABLE=1 \
            ib_write_bw -d "$DEV" -x "$GID_INDEX" -s "$msg_size" -D "$DURATION" --report_gbits &
    else
        ib_write_bw -d "$DEV" -x "$GID_INDEX" -s "$msg_size" -D "$DURATION" --report_gbits &
    fi
    local server_pid=$!
    
    # 等待server启动
    sleep 2
    
    # 在远程运行client
    if [ "$use_intercept" -eq 1 ]; then
        run_remote "cd $PROJECT_DIR && LD_PRELOAD=$INTERCEPT_LIB RDMA_INTERCEPT_ENABLE=1 \
            ib_write_bw -d $DEV -x $GID_INDEX -s $msg_size -D $DURATION --report_gbits $LOCAL_RDMA_IP" 2>&1 | \
            grep -E "^[0-9]+\s+[0-9]+" | tail -1
    else
        run_remote "ib_write_bw -d $DEV -x $GID_INDEX -s $msg_size -D $DURATION --report_gbits $LOCAL_RDMA_IP" 2>&1 | \
            grep -E "^[0-9]+\s+[0-9]+" | tail -1
    fi
    
    # 等待server结束
    wait $server_pid 2>/dev/null || true
    
    # 解析结果并保存
    # 这里简化处理，实际应该解析ib_write_bw的输出
    # 由于远程执行复杂，我们使用模拟数据或本地测试
}

echo "========================================"
echo "1. 基线测试 (无LD_PRELOAD)"
echo "========================================"
for size in "${MSG_SIZES[@]}"; do
    for i in $(seq 1 $ITERATIONS); do
        echo "测试消息大小: $size bytes (第$i/$ITERATIONS轮)"
        # 启动server
        ib_write_bw -d "$DEV" -x "$GID_INDEX" -s "$size" -D "$DURATION" --report_gbits > /tmp/bw_server.log 2>&1 &
        server_pid=$!
        sleep 2
        
        # 运行client并获取结果
        result=$(run_remote "ib_write_bw -d $DEV -x $GID_INDEX -s $size -D $DURATION --report_gbits $LOCAL_RDMA_IP" 2>&1)
        echo "$result"
        
        # 解析带宽值
        bw=$(echo "$result" | grep -E "^[0-9]+\s+[0-9]+" | awk '{print $2}' | tail -1)
        if [ -z "$bw" ]; then
            bw="N/A"
        fi
        echo "  带宽: $bw Gbps"
        
        # 保存结果
        echo "$size,$bw,0" >> "$BASELINE_CSV"
        
        # 清理
        kill $server_pid 2>/dev/null || true
        sleep 1
    done
done

echo ""
echo "========================================"
echo "2. 拦截测试 (有LD_PRELOAD)"
echo "========================================"
for size in "${MSG_SIZES[@]}"; do
    for i in $(seq 1 $ITERATIONS); do
        echo "测试消息大小: $size bytes (第$i/$ITERATIONS轮)"
        # 启动server（带拦截）
        LD_PRELOAD="$INTERCEPT_LIB" RDMA_INTERCEPT_ENABLE=1 \
            ib_write_bw -d "$DEV" -x "$GID_INDEX" -s "$size" -D "$DURATION" --report_gbits > /tmp/bw_server_intercept.log 2>&1 &
        server_pid=$!
        sleep 2
        
        # 运行client（带拦截）
        result=$(run_remote "cd $PROJECT_DIR && LD_PRELOAD=$INTERCEPT_LIB RDMA_INTERCEPT_ENABLE=1 \
            ib_write_bw -d $DEV -x $GID_INDEX -s $size -D $DURATION --report_gbits $LOCAL_RDMA_IP" 2>&1)
        echo "$result"
        
        # 解析带宽值
        bw=$(echo "$result" | grep -E "^[0-9]+\s+[0-9]+" | awk '{print $2}' | tail -1)
        if [ -z "$bw" ]; then
            bw="N/A"
        fi
        echo "  带宽: $bw Gbps"
        
        # 保存结果
        echo "$size,$bw,0" >> "$INTERCEPT_CSV"
        
        # 清理
        kill $server_pid 2>/dev/null || true
        sleep 1
    done
done

echo ""
echo "========================================"
echo "实验完成!"
echo "========================================"
echo "结果文件:"
echo "  基线: $BASELINE_CSV"
echo "  拦截: $INTERCEPT_CSV"
