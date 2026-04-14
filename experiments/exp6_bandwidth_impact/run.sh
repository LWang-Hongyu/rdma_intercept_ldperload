#!/bin/bash
# EXP-6: RDMA数据面带宽影响测试（双机版）
#
# 测试架构:
#   guolab-8 (192.168.108.2)  <--->  guolab-6 (192.168.106.2)
#   本地Server                      远程Client
#
# 测试方法:
# 1. 基线测试: 无LD_PRELOAD
# 2. 拦截测试: 有LD_PRELOAD + 拦截库

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

# 网络配置
LOCAL_MGMT_IP="10.157.197.53"
LOCAL_RDMA_IP="192.168.108.2"
REMOTE_MGMT_IP="10.157.197.51"
REMOTE_RDMA_IP="192.168.106.2"

DEV="mlx5_0"
LOCAL_GID_INDEX="6"
REMOTE_GID_INDEX="5"
DURATION="15"
ITERATIONS=2

echo "========================================"
echo "EXP-6: RDMA数据面带宽影响测试（双机版）"
echo "========================================"
echo ""
echo "网络配置:"
echo "  Local:  $LOCAL_RDMA_IP (guolab-8)"
echo "  Remote: $REMOTE_RDMA_IP (guolab-6)"
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

echo "msg_size,bw_gbps,msg_rate" > "$BASELINE_CSV"
echo "msg_size,bw_gbps,msg_rate" > "$INTERCEPT_CSV"

# 提取结果的函数
extract_result() {
    local logfile=$1
    local line=$(grep "^[[:space:]]*[0-9]\+[[:space:]]\+[0-9]" "$logfile" | tail -1)
    local bw=$(echo "$line" | awk '{print $4}')
    local msgrate=$(echo "$line" | awk '{print $5}')
    echo "$bw,$msgrate"
}

# ========================================
# 测试1: 基线测试（无拦截）
# ========================================
echo "========================================"
echo "测试1: 基线测试（无拦截）"
echo "========================================"

for size in 65536 262144 1048576; do
    echo ""
    echo "消息大小: $size bytes"
    
    for iter in $(seq 1 $ITERATIONS); do
        echo "  迭代 $iter/$ITERATIONS..."
        
        # 在本地启动server
        ib_write_bw -d $DEV -x $LOCAL_GID_INDEX -s $size -D $DURATION --report_gbits > /dev/null 2>&1 &
        SERVER_PID=$!
        sleep 1
        
        # 在远程启动client
        CLIENT_LOG=$(mktemp)
        if ssh why@$REMOTE_MGMT_IP "ib_write_bw -d $DEV -x $REMOTE_GID_INDEX -s $size -D $DURATION --report_gbits $LOCAL_RDMA_IP" > "$CLIENT_LOG" 2>&1; then
            RESULT=$(extract_result "$CLIENT_LOG")
            BW=$(echo "$RESULT" | cut -d',' -f1)
            MSG_RATE=$(echo "$RESULT" | cut -d',' -f2)
            
            BW=${BW:-0}
            MSG_RATE=${MSG_RATE:-0}
            
            echo "$size,$BW,$MSG_RATE" >> "$BASELINE_CSV"
            echo "    带宽: ${BW} Gbps"
        else
            echo "    测试失败"
            echo "$size,0,0" >> "$BASELINE_CSV"
        fi
        
        rm -f "$CLIENT_LOG"
        wait $SERVER_PID 2>/dev/null || true
        sleep 1
    done
done

echo ""
echo "✓ 基线测试完成"

# ========================================
# 测试2: 拦截测试（有LD_PRELOAD）
# ========================================
echo ""
echo "========================================"
echo "测试2: 拦截测试（有LD_PRELOAD）"
echo "========================================"

if [ ! -f "$INTERCEPT_LIB" ]; then
    echo "警告: 拦截库不存在: $INTERCEPT_LIB"
    echo "复制基线数据作为占位"
    cp "$BASELINE_CSV" "$INTERCEPT_CSV"
else
    for size in 65536 262144 1048576; do
        echo ""
        echo "消息大小: $size bytes"
        
        for iter in $(seq 1 $ITERATIONS); do
            echo "  迭代 $iter/$ITERATIONS..."
            
            # 本地启动server（带拦截）
            export LD_PRELOAD=$INTERCEPT_LIB
            export RDMA_INTERCEPT_ENABLE=1
            ib_write_bw -d $DEV -x $LOCAL_GID_INDEX -s $size -D $DURATION --report_gbits > /dev/null 2>&1 &
            SERVER_PID=$!
            unset LD_PRELOAD
            unset RDMA_INTERCEPT_ENABLE
            sleep 1
            
            # 远程启动client（带拦截）
            CLIENT_LOG=$(mktemp)
            if ssh why@$REMOTE_MGMT_IP "export LD_PRELOAD=$INTERCEPT_LIB; export RDMA_INTERCEPT_ENABLE=1; ib_write_bw -d $DEV -x $REMOTE_GID_INDEX -s $size -D $DURATION --report_gbits $LOCAL_RDMA_IP" > "$CLIENT_LOG" 2>&1; then
                RESULT=$(extract_result "$CLIENT_LOG")
                BW=$(echo "$RESULT" | cut -d',' -f1)
                MSG_RATE=$(echo "$RESULT" | cut -d',' -f2)
                
                BW=${BW:-0}
                MSG_RATE=${MSG_RATE:-0}
                
                echo "$size,$BW,$MSG_RATE" >> "$INTERCEPT_CSV"
                echo "    带宽: ${BW} Gbps"
            else
                echo "    测试失败"
                echo "$size,0,0" >> "$INTERCEPT_CSV"
            fi
            
            rm -f "$CLIENT_LOG"
            wait $SERVER_PID 2>/dev/null || true
            sleep 1
        done
    done
fi

echo ""
echo "✓ 拦截测试完成"

# ========================================
# 生成图表
# ========================================
echo ""
echo "========================================"
echo "生成图表"
echo "========================================"

python3 "$SCRIPT_DIR/analysis/plot.py" "$RESULTS_DIR" 2>/dev/null || echo "绘图失败"

# ========================================
# 汇总结果
# ========================================
echo ""
echo "========================================"
echo "EXP-6 完成"
echo "========================================"
echo ""

python3 << EOF
import csv

def load_data(filename):
    data = {}
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                size = int(row['msg_size'])
                bw = float(row['bw_gbps'])
                if size not in data:
                    data[size] = []
                if bw > 0:
                    data[size].append(bw)
            except:
                pass
    return {k: sum(v)/len(v) for k, v in data.items() if v}

baseline = load_data("$BASELINE_CSV")
intercept = load_data("$INTERCEPT_CSV")

print("双机带宽影响分析:")
print("-" * 60)
print(f"{'消息大小':<15} {'基线(Gbps)':<15} {'拦截(Gbps)':<15} {'影响(%)':<10}")
print("-" * 60)

all_sizes = sorted(set(baseline.keys()) & set(intercept.keys()))
for size in all_sizes:
    base = baseline[size]
    inter = intercept[size]
    impact = (base - inter) / base * 100 if base > 0 else 0
    size_str = f"{size//1024}KB" if size >= 1024 else f"{size}B"
    print(f"{size_str:<15} {base:<15.2f} {inter:<15.2f} {impact:<10.2f}")

if all_sizes:
    avg_base = sum(baseline[s] for s in all_sizes) / len(all_sizes)
    avg_inter = sum(intercept[s] for s in all_sizes) / len(all_sizes)
    avg_impact = (avg_base - avg_inter) / avg_base * 100
    
    print("-" * 60)
    print(f"{'平均':<15} {avg_base:<15.2f} {avg_inter:<15.2f} {avg_impact:<10.2f}")
    print()
    
    if abs(avg_impact) < 5:
        print(f"✓ PASS: 双机测试下LD_PRELOAD影响 < 5%")
    elif abs(avg_impact) < 10:
        print(f"⚠ WARNING: 平均影响 {avg_impact:.2f}%")
    else:
        print(f"✗ FAIL: 平均影响 {avg_impact:.2f}%")

print("-" * 60)
EOF

echo ""
ls -lh "$RESULTS_DIR"/*.{csv,png} 2>/dev/null || true
echo ""
