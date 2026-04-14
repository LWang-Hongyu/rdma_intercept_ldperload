#!/bin/bash
# EXP-1: 微基准测试
# 测试QP、MR、CQ的拦截开销

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "=========================================="
echo "EXP-1: Microbenchmark (QP/MR/CQ Overhead)"
echo "=========================================="
echo "Results will be saved to: $RESULTS_DIR"
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
echo "[Run] Testing with interception (LD_PRELOAD)..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" \
    "$SCRIPT_DIR/microbenchmark" --output="$RESULTS_DIR/with_intercept.csv"

echo ""
echo "=========================================="
echo "Results saved to:"
echo "  - $RESULTS_DIR/baseline.csv"
echo "  - $RESULTS_DIR/with_intercept.csv"
echo ""
echo "To generate plots:"
echo "  python3 $SCRIPT_DIR/analysis/plot.py"
echo "=========================================="
