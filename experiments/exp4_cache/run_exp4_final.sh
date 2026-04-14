#!/bin/bash
# EXP-4: 缓存性能评估（修正版本）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
SRC_DIR="$SCRIPT_DIR/src"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "========================================"
echo "EXP-4: 缓存性能评估"
echo "========================================"
echo ""

# 设置库路径
export LD_LIBRARY_PATH="$PROJECT_DIR/build:$LD_LIBRARY_PATH"

# 创建目录
mkdir -p "$RESULTS_DIR"
mkdir -p "$SCRIPT_DIR/build"

# 编译
echo "[Build] 编译测试程序..."

# 直接编译，不链接库
gcc -O2 -o "$SCRIPT_DIR/build/exp4_cache" \
    "$SRC_DIR/exp4_cache.c" \
    "$PROJECT_DIR/src/performance_optimizer.c" \
    "$PROJECT_DIR/src/shm/shared_memory.c" \
    "$PROJECT_DIR/src/shm/shared_memory_tenant.c" \
    -I"$PROJECT_DIR" \
    -I"$PROJECT_DIR/include" \
    -I"$PROJECT_DIR/src" \
    -lpthread -lrt -lm || {
    echo "编译失败"; 
    exit 1;
}

echo "  ✅ 编译完成"
echo ""

# ========================================
# 测试1: 缓存命中率
# ========================================
echo "========================================"
echo "测试1: 缓存命中率"
echo "========================================"

for workload in 0 1 2; do
    workload_name=""
    case $workload in
        0) workload_name="sequential" ;;
        1) workload_name="random" ;;
        2) workload_name="temporal" ;;
    esac
    
    echo ""
    echo "工作负载: $workload_name (type=$workload)"
    echo "----------------------------------------"
    
    for ttl in 50 100 200; do
        echo "  TTL=${ttl}ms..."
        "$SCRIPT_DIR/build/exp4_cache" \
            --test-type=0 \
            --workload=$workload \
            --processes=100 \
            --ttl=$ttl \
            --output="$RESULTS_DIR/exp4_hitrate_${workload_name}_ttl${ttl}.csv"
    done
done

# ========================================
# 测试2: 延迟对比（缓存 vs 共享内存）
# ========================================
echo ""
echo "========================================"
echo "测试2: 延迟对比"
echo "========================================"

# 共享内存（无缓存）
echo ""
echo "场景: 共享内存（无缓存）"
echo "----------------------------------------"
"$SCRIPT_DIR/build/exp4_cache" \
    --test-type=1 \
    --cache=0 \
    --output="$RESULTS_DIR/exp4_latency_nocache.csv"

# 使用缓存（不同TTL）
echo ""
echo "场景: 使用缓存"
echo "----------------------------------------"
for ttl in 50 100 200 500; do
    echo "  TTL=${ttl}ms..."
    "$SCRIPT_DIR/build/exp4_cache" \
        --test-type=1 \
        --cache=1 \
        --ttl=$ttl \
        --output="$RESULTS_DIR/exp4_ttl${ttl}ms.csv"
done

# ========================================
# 测试3: 自适应TTL效果
# ========================================
echo ""
echo "========================================"
echo "测试3: 自适应TTL效果"
echo "========================================"

# 固定TTL
echo ""
echo "场景: 固定TTL=100ms"
echo "----------------------------------------"
"$SCRIPT_DIR/build/exp4_cache" \
    --test-type=2 \
    --adaptive=0 \
    --ttl=100 \
    --output="$RESULTS_DIR/exp4_adaptive_fixed.csv"

# 自适应TTL
echo ""
echo "场景: 自适应TTL"
echo "----------------------------------------"
"$SCRIPT_DIR/build/exp4_cache" \
    --test-type=2 \
    --adaptive=1 \
    --ttl=100 \
    --output="$RESULTS_DIR/exp4_adaptive_enabled.csv"

# ========================================
# 生成图表
# ========================================
echo ""
echo "========================================"
echo "生成图表"
echo "========================================"

if command -v python3 &> /dev/null; then
    if [ -f "$SCRIPT_DIR/analysis/plot.py" ]; then
        python3 "$SCRIPT_DIR/analysis/plot.py" "$RESULTS_DIR"
    else
        echo "绘图脚本不存在，跳过"
    fi
else
    echo "Python3未安装，跳过图表生成"
fi

# ========================================
# 汇总结果
# ========================================
echo ""
echo "========================================"
echo "EXP-4 完成"
echo "========================================"
echo ""
echo "结果文件:"
ls -lh "$RESULTS_DIR"/exp4_*.csv 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
echo ""
echo "图表文件:"
ls -lh "$RESULTS_DIR"/exp4_*.png 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}' || echo "  无"
echo ""
