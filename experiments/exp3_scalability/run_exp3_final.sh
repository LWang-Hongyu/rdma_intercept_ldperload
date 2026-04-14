#!/bin/bash
# EXP-3: 可扩展性测试（修正版本 - 使用共享内存机制）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "========================================"
echo "EXP-3: 可扩展性测试"
echo "========================================"
echo ""
echo "设计:"
echo "  ✓ 固定每租户QP数 = 10"
echo "  ✓ 改变租户数: 1, 5, 10, 20, 50"
echo "  ✓ 预热1个QP消除冷启动"
echo "  ✓ 对比: 无拦截 / 有拦截"
echo ""

# 编译
echo "[Build] 编译..."
gcc -O2 -o "$SCRIPT_DIR/build/exp3_scalability" "$SCRIPT_DIR/src/exp3_scalability.c" -libverbs || {
    echo "编译失败"; exit 1;
}
echo "✅ 编译完成"

mkdir -p "$RESULTS_DIR"

# 测试配置（减少规模以避免资源耗尽）
TENANT_COUNTS=(1 5 10 20 50)

echo ""
echo "========================================"
echo "场景1: 无拦截 (Baseline)"
echo "========================================"
echo ""

for tenants in "${TENANT_COUNTS[@]}"; do
    echo "----------------------------------------"
    echo "测试: $tenants 租户 × 10 QP = $((tenants * 10)) QP"
    echo "----------------------------------------"
    
    output="$RESULTS_DIR/exp3_${tenants}t_baseline.csv"
    
    "$SCRIPT_DIR/build/exp3_scalability" --tenants=$tenants --warmup=1 --output=$output
    
    echo ""
done

echo ""
echo "========================================"
echo "场景2: 有拦截 (启用QP控制)"
echo "========================================"
echo ""

export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1
export RDMA_INTERCEPT_ENABLE=1

for tenants in "${TENANT_COUNTS[@]}"; do
    echo "----------------------------------------"
    echo "测试: $tenants 租户 × 10 QP = $((tenants * 10)) QP"
    echo "----------------------------------------"
    
    output="$RESULTS_DIR/exp3_${tenants}t_intercept.csv"
    
    LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" \
        "$SCRIPT_DIR/build/exp3_scalability" --tenants=$tenants --warmup=1 --output=$output
    
    echo ""
done

echo ""
echo "========================================"
echo "生成图表..."
echo "========================================"
echo ""

python3 "$SCRIPT_DIR/analysis/plot.py" "$RESULTS_DIR"

echo ""
echo "========================================"
echo "实验完成"
echo "========================================"
echo ""
echo "结果文件:"
ls -lh "$RESULTS_DIR"/exp3_*.csv "$RESULTS_DIR"/exp3_*.png 2>/dev/null || true
