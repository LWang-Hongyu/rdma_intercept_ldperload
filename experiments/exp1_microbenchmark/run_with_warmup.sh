#!/bin/bash
# EXP-1: 微基准测试 - 带额外预热确保拦截系统初始化完成

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "=========================================="
echo "EXP-1: Microbenchmark (with proper warmup)"
echo "=========================================="
echo ""

mkdir -p "$RESULTS_DIR"

# 编译测试程序
if [ ! -f "$SCRIPT_DIR/microbenchmark" ]; then
    echo "[Build] Compiling microbenchmark..."
    gcc -O2 -o "$SCRIPT_DIR/microbenchmark" \
        "$SCRIPT_DIR/src/exp1_microbenchmark.c" -libverbs -lpthread -lm || {
        echo "[ERROR] Failed to compile"
        exit 1
    }
fi

echo "[Run] Testing without interception..."
"$SCRIPT_DIR/microbenchmark" --output="$RESULTS_DIR/baseline.csv"

echo ""
echo "[Run] Pre-warming interception system..."
# 先运行一次，让拦截系统完成初始化
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" \
    "$SCRIPT_DIR/microbenchmark" --output="/tmp/warmup.csv" 2>/dev/null
echo "  Intercept system warmed up"

echo ""
echo "[Run] Testing with interception (after warmup)..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" \
    "$SCRIPT_DIR/microbenchmark" --output="$RESULTS_DIR/with_intercept.csv"

echo ""
echo "=========================================="
echo "Results saved to:"
echo "  - $RESULTS_DIR/baseline.csv"
echo "  - $RESULTS_DIR/with_intercept.csv"
echo "=========================================="
