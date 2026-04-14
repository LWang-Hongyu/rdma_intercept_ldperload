#!/bin/bash
# EXP-3: 可扩展性测试（严谨版本）
# 
# 设计:
# - 固定每租户QP数 = 10
# - 改变租户数: 1, 5, 10, 20, 50, 100
# - 预热1个QP消除冷启动
# - 三种场景: 无拦截 / quota=5 / quota=50

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "========================================"
echo "EXP-3: 可扩展性测试（严谨版）"
echo "========================================"
echo ""
echo "设计改进:"
echo "  ✓ 固定每租户QP数 = 10（单一变量）"
echo "  ✓ 预热1个QP消除冷启动"
echo "  ✓ 对比: 无拦截 / quota=5 / quota=50"
echo ""

# 编译
echo "[Build] 编译..."
gcc -O2 -o "$SCRIPT_DIR/build/exp3_scalability" "$SCRIPT_DIR/src/exp3_scalability.c" -libverbs || {
    echo "编译失败"; exit 1;
}
echo "✅ 编译完成"

mkdir -p "$RESULTS_DIR"

# 测试配置
TENANT_COUNTS=(1 5 10 20 50 100)

echo ""
echo "========================================"
echo "场景1: 无拦截 (Baseline)"
echo "========================================"
echo ""

for tenants in "${TENANT_COUNTS[@]}"; do
    echo "----------------------------------------"
    echo "测试: $tenants 租户 × 10 QP = $((tenants * 10)) QP"
    echo "----------------------------------------"
    
    output="$RESULTS_DIR/exp3v2_${tenants}t_baseline.csv"
    
    "$SCRIPT_DIR/build/exp3_scalability" --tenants=$tenants --warmup=1 --output=$output
    
    echo ""
done

echo ""
echo "========================================"
echo "场景2: 有拦截 (quota=5, 严格)"
echo "========================================"
echo ""

echo "[启动] 拦截守护进程 (quota-qp=5)..."
sudo "$PROJECT_DIR/build/librdma_intercept_daemon" --quota-qp=5 &
DAEMON_PID=$!
sleep 2

for tenants in "${TENANT_COUNTS[@]}"; do
    # 跳过超过配额的（如果每租户10QP > quota=5）
    if [ 10 -gt 5 ]; then
        echo "跳过: $tenants 租户 (10QP/tenant > quota=5)"
        continue
    fi
    
    echo "----------------------------------------"
    echo "测试: $tenants 租户 × 10 QP"
    echo "----------------------------------------"
    
    output="$RESULTS_DIR/exp3v2_${tenants}t_quota5.csv"
    
    LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" \
        "$SCRIPT_DIR/build/exp3_scalability" --tenants=$tenants --warmup=1 --output=$output
    
    echo ""
done

sudo kill $DAEMON_PID 2>/dev/null || true

echo ""
echo "========================================"
echo "场景3: 有拦截 (quota=50, 宽松)"
echo "========================================"
echo ""

echo "[启动] 拦截守护进程 (quota-qp=50)..."
sudo "$PROJECT_DIR/build/librdma_intercept_daemon" --quota-qp=50 &
DAEMON_PID=$!
sleep 2

for tenants in "${TENANT_COUNTS[@]}"; do
    echo "----------------------------------------"
    echo "测试: $tenants 租户 × 10 QP"
    echo "----------------------------------------"
    
    output="$RESULTS_DIR/exp3v2_${tenants}t_quota50.csv"
    
    LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" \
        "$SCRIPT_DIR/build/exp3_scalability" --tenants=$tenants --warmup=1 --output=$output
    
    echo ""
done

sudo kill $DAEMON_PID 2>/dev/null || true

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
ls -lh "$RESULTS_DIR"/exp3v2_*.csv "$RESULTS_DIR"/exp3v2_*.png 2>/dev/null || true
