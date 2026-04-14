#!/bin/bash
# EXP-1: 生成实验报告（无需Python）

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$(dirname "$SCRIPT_DIR")/results"

echo "=========================================="
echo "EXP-1: 微基准测试报告"
echo "=========================================="
echo ""

# 读取baseline结果
echo "【基线测试 (无拦截)】"
echo "------------------------------"
if [ -f "$RESULTS_DIR/baseline.csv" ]; then
    cat "$RESULTS_DIR/baseline.csv"
else
    echo "基线结果文件不存在"
fi

echo ""
echo "【拦截测试 (有LD_PRELOAD)】"
echo "------------------------------"
if [ -f "$RESULTS_DIR/with_intercept.csv" ]; then
    cat "$RESULTS_DIR/with_intercept.csv"
else
    echo "拦截结果文件不存在"
fi

echo ""
echo "=========================================="
echo "性能开销分析"
echo "=========================================="

# 提取数值并计算开销
if [ -f "$RESULTS_DIR/baseline.csv" ] && [ -f "$RESULTS_DIR/with_intercept.csv" ]; then
    # 提取QP创建延迟均值
    BASELINE_QP=$(grep "^MEAN:" "$RESULTS_DIR/baseline.csv" | head -1 | awk '{print $2}')
    INTERCEPT_QP=$(grep "^MEAN:" "$RESULTS_DIR/with_intercept.csv" | head -1 | awk '{print $2}')
    
    # 提取MR注册延迟均值（第3个MEAN）
    BASELINE_MR=$(grep "^MEAN:" "$RESULTS_DIR/baseline.csv" | tail -1 | awk '{print $2}')
    INTERCEPT_MR=$(grep "^MEAN:" "$RESULTS_DIR/with_intercept.csv" | tail -1 | awk '{print $2}')
    
    echo ""
    echo "QP创建延迟:"
    echo "  基线:    ${BASELINE_QP} us"
    echo "  拦截:    ${INTERCEPT_QP} us"
    
    # 计算百分比变化
    if command -v bc >/dev/null 2>&1; then
        QP_OVERHEAD=$(echo "scale=2; (${INTERCEPT_QP} - ${BASELINE_QP}) / ${BASELINE_QP} * 100" | bc)
        echo "  开销:    ${QP_OVERHEAD}%"
    else
        QP_OVERHEAD=$(awk "BEGIN {printf \"%.2f\", (${INTERCEPT_QP} - ${BASELINE_QP}) / ${BASELINE_QP} * 100}")
        echo "  开销:    ${QP_OVERHEAD}%"
    fi
    
    echo ""
    echo "MR注册延迟:"
    echo "  基线:    ${BASELINE_MR} us"
    echo "  拦截:    ${INTERCEPT_MR} us"
    
    if command -v bc >/dev/null 2>&1; then
        MR_OVERHEAD=$(echo "scale=2; (${INTERCEPT_MR} - ${BASELINE_MR}) / ${BASELINE_MR} * 100" | bc)
        echo "  开销:    ${MR_OVERHEAD}%"
    else
        MR_OVERHEAD=$(awk "BEGIN {printf \"%.2f\", (${INTERCEPT_MR} - ${BASELINE_MR}) / ${BASELINE_MR} * 100}")
        echo "  开销:    ${MR_OVERHEAD}%"
    fi
    
    echo ""
    echo "=========================================="
    echo "结论"
    echo "=========================================="
    
    # 判断是否满足小于20%的要求
    QP_OVERHEAD_NUM=$(echo "$QP_OVERHEAD" | sed 's/-//')
    MR_OVERHEAD_NUM=$(echo "$MR_OVERHEAD" | sed 's/-//')
    
    # 使用awk进行比较
    QP_PASS=$(awk -v val="$QP_OVERHEAD_NUM" 'BEGIN {print (val < 20) ? 1 : 0}')
    MR_PASS=$(awk -v val="$MR_OVERHEAD_NUM" 'BEGIN {print (val < 20) ? 1 : 0}')
    
    if [ "$QP_PASS" = "1" ]; then
        echo "✓ QP创建拦截开销 (${QP_OVERHEAD}%) < 20%，满足设计要求"
    else
        echo "✗ QP创建拦截开销 (${QP_OVERHEAD}%) >= 20%，需要优化"
    fi
    
    if [ "$MR_PASS" = "1" ]; then
        echo "✓ MR注册拦截开销 (${MR_OVERHEAD}%) < 20%，满足设计要求"
    else
        echo "✗ MR注册拦截开销 (${MR_OVERHEAD}%) >= 20%，需要优化"
    fi
fi

echo ""
echo "=========================================="
echo "详细结果保存在:"
echo "  $RESULTS_DIR/baseline.csv"
echo "  $RESULTS_DIR/with_intercept.csv"
echo "=========================================="
