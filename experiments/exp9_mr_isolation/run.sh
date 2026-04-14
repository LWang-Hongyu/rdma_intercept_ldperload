#!/bin/bash
# EXP-9: MR资源隔离验证测试

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "=========================================="
echo "EXP-9: MR资源隔离验证"
echo "=========================================="
echo ""

mkdir -p "$RESULTS_DIR"

# 编译测试程序
if [ ! -f "$SCRIPT_DIR/exp9_mr_isolation" ]; then
    echo "[Build] Compiling exp9_mr_isolation..."
    gcc -O2 -o "$SCRIPT_DIR/exp9_mr_isolation" \
        "$SCRIPT_DIR/src/exp9_mr_isolation.c" -libverbs -lpthread || {
        echo "[ERROR] Failed to compile"
        exit 1
    }
fi

echo "[Test] 场景1: 无拦截（基线）"
unset LD_PRELOAD
"$SCRIPT_DIR/exp9_mr_isolation" -t 10 -n 50 -s 1048576 -o "$RESULTS_DIR/baseline.txt"

echo ""
echo "[Test] 场景2: 有拦截，限制每租户MR=10"
export LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so"
export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_ENABLE_MR_CONTROL=1
export RDMA_INTERCEPT_MAX_MR_PER_PROCESS=10
export RDMA_TENANT_ID=10
"$SCRIPT_DIR/exp9_mr_isolation" -t 10 -n 50 -s 1048576 -o "$RESULTS_DIR/with_limit.txt"
unset LD_PRELOAD

echo ""
echo "=========================================="
echo "实验完成！"
echo "结果保存在:"
echo "  - $RESULTS_DIR/baseline.txt"
echo "  - $RESULTS_DIR/with_limit.txt"
echo "=========================================="
