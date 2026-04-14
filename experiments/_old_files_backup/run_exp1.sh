#!/bin/bash
# EXP-1 自动化运行脚本
# 用途: 自动化运行微基准测试并生成报告

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
RESULTS_DIR="$PROJECT_DIR/paper_results/exp1"
BENCHMARK="$RESULTS_DIR/benchmark"

echo "============================================================"
echo "EXP-1: RDMA资源操作微基准测试"
echo "============================================================"
echo ""

# 检查环境
echo "[1/5] 检查实验环境..."
if ! command -v ibstat &> /dev/null; then
    echo "错误: 未找到ibstat命令，请确保RDMA环境已配置"
    exit 1
fi

if ! ibstat -l | grep -q "mlx"; then
    echo "错误: 未找到Mellanox RDMA设备"
    exit 1
fi

echo "  ✓ RDMA设备正常: $(ibstat -l | head -1)"

# 准备目录
echo ""
echo "[2/5] 准备实验目录..."
mkdir -p "$RESULTS_DIR"
echo "  ✓ 结果目录: $RESULTS_DIR"

# 编译测试程序
echo ""
echo "[3/5] 编译测试程序..."
if [ ! -f "$BENCHMARK" ] || [ "$SCRIPT_DIR/exp1_microbenchmark.c" -nt "$BENCHMARK" ]; then
    gcc -O2 -o "$BENCHMARK" "$SCRIPT_DIR/exp1_microbenchmark.c" -libverbs -lm
    echo "  ✓ 编译完成"
else
    echo "  ✓ 测试程序已是最新"
fi

# 运行基线测试
echo ""
echo "[4/5] 运行基线测试 (无拦截)..."
unset LD_PRELOAD
unset RDMA_INTERCEPT_ENABLE
$BENCHMARK "$RESULTS_DIR/baseline.txt" 2>&1 | tee "$RESULTS_DIR/baseline.log"
echo "  ✓ 基线测试完成，结果保存至 baseline.txt"

# 运行拦截测试
echo ""
echo "[5/5] 运行拦截测试 (有拦截)..."
export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1
export RDMA_INTERCEPT_MAX_QP_PER_PROCESS=20000
export LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so"

$BENCHMARK "$RESULTS_DIR/intercept.txt" 2>&1 | tee "$RESULTS_DIR/intercept.log"
echo "  ✓ 拦截测试完成，结果保存至 intercept.txt"

# 运行分析
echo ""
echo "============================================================"
echo "运行结果分析..."
echo "============================================================"
cd "$RESULTS_DIR"
python3 analysis.py 2>/dev/null || echo "  注意: 分析脚本运行失败，请手动运行 'python3 analysis.py'"

echo ""
echo "============================================================"
echo "EXP-1 实验完成!"
echo "============================================================"
echo "结果文件:"
echo "  - $RESULTS_DIR/baseline.txt"
echo "  - $RESULTS_DIR/intercept.txt"
echo "  - $RESULTS_DIR/README.md"
echo "============================================================"
