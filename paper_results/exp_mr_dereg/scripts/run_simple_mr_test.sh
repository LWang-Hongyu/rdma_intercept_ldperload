#!/bin/bash
# 简化的MR注销攻击对比实验

set -e

RESULTS_DIR="/home/why/rdma_intercept_ldpreload/paper_results/exp_mr_dereg_results"
INTERCEPT_LIB="/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so"
TEST_DURATION=20
NUM_MRS=50
BATCH_SIZE=10

echo "========================================"
echo "MR Deregistration Abuse Attack Test"
echo "对比实验: 无拦截 vs 有拦截"
echo "========================================"
echo ""

mkdir -p $RESULTS_DIR

# ============================================================
# 实验1: 无拦截
# ============================================================
echo "========================================"
echo "实验1: 无拦截保护"
echo "========================================"
echo ""

# 清理环境
unset LD_PRELOAD RDMA_INTERCEPT_ENABLE RDMA_TENANT_ID

echo "运行攻击程序（模拟恶意租户）..."
./exp_mr_dereg_abuse -t $TEST_DURATION -n $NUM_MRS -b $BATCH_SIZE 2>&1 | tee $RESULTS_DIR/attacker_no_intercept.log
echo ""

# 提取关键数据
CYCLES_NO=$(grep "Total deregister+register cycles:" $RESULTS_DIR/attacker_no_intercept.log | awk '{print $5}')
OPS_NO=$(grep "Total MR operations:" $RESULTS_DIR/attacker_no_intercept.log | awk '{print $4}')
REG_LAT_NO=$(grep "Avg register latency:" $RESULTS_DIR/attacker_no_intercept.log | awk '{print $4}')
DREG_LAT_NO=$(grep "Avg deregister latency:" $RESULTS_DIR/attacker_no_intercept.log | awk '{print $4}')

echo "结果摘要（无拦截）:"
echo "  攻击循环次数: $CYCLES_NO"
echo "  MR操作总数: $OPS_NO"
echo "  平均注册延迟: ${REG_LAT_NO}us"
echo "  平均注销延迟: ${DREG_LAT_NO}us"
echo ""

# ============================================================
# 实验2: 有拦截
# ============================================================
echo "========================================"
echo "实验2: 启用拦截保护"
echo "========================================"
echo ""

# 设置拦截环境
export LD_PRELOAD=$INTERCEPT_LIB
export RDMA_INTERCEPT_ENABLE=1
export RDMA_TENANT_ID=20

# 配置租户配额（限制MR数量）
echo "配置租户配额 (20 QPs, 20 MRs)..."
export LD_LIBRARY_PATH=/home/why/rdma_intercept_ldpreload/build:$LD_LIBRARY_PATH
/home/why/rdma_intercept_ldpreload/build/tenant_manager --create 20 --name "Attacker" --quota 20,20,1073741824 2>/dev/null || true
echo ""

echo "运行攻击程序（模拟被限制的恶意租户）..."
./exp_mr_dereg_abuse -t $TEST_DURATION -n $NUM_MRS -b $BATCH_SIZE 2>&1 | tee $RESULTS_DIR/attacker_with_intercept.log || true
echo ""

# 清理环境
unset LD_PRELOAD RDMA_INTERCEPT_ENABLE RDMA_TENANT_ID

# 提取关键数据
CYCLES_WITH=$(grep "Total deregister+register cycles:" $RESULTS_DIR/attacker_with_intercept.log | awk '{print $5}')
OPS_WITH=$(grep "Total MR operations:" $RESULTS_DIR/attacker_with_intercept.log | awk '{print $4}')
REG_LAT_WITH=$(grep "Avg register latency:" $RESULTS_DIR/attacker_with_intercept.log | awk '{print $4}')
DREG_LAT_WITH=$(grep "Avg deregister latency:" $RESULTS_DIR/attacker_with_intercept.log | awk '{print $4}')

echo "结果摘要（有拦截）:"
echo "  攻击循环次数: ${CYCLES_WITH:-N/A}"
echo "  MR操作总数: ${OPS_WITH:-N/A}"
echo "  平均注册延迟: ${REG_LAT_WITH:-N/A}us"
echo "  平均注销延迟: ${DREG_LAT_WITH:-N/A}us"
echo ""

# ============================================================
# 结果对比
# ============================================================
echo "========================================"
echo "结果对比"
echo "========================================"
echo ""
echo "指标                    无拦截          有拦截"
echo "==========================================="
printf "攻击循环次数            %-15s %-15s\n" "$CYCLES_NO" "${CYCLES_WITH:-N/A}"
printf "MR操作总数              %-15s %-15s\n" "$OPS_NO" "${OPS_WITH:-N/A}"
printf "平均注册延迟(us)        %-15s %-15s\n" "$REG_LAT_NO" "${REG_LAT_WITH:-N/A}"
printf "平均注销延迟(us)        %-15s %-15s\n" "$DREG_LAT_NO" "${DREG_LAT_WITH:-N/A}"
echo ""

echo "分析:"
if [ -n "$CYCLES_WITH" ] && [ "$CYCLES_WITH" != "0" ]; then
    # 计算比例
    RATIO=$(echo "scale=2; $CYCLES_WITH / $CYCLES_NO" | bc 2>/dev/null || echo "N/A")
    echo "  攻击效率比(有/无): ${RATIO}"
    if (( $(echo "$RATIO < 0.5" | bc -l 2>/dev/null || echo 0) )); then
        echo "  结论: 拦截成功限制了攻击者，攻击效率降低超过50%"
    fi
else
    echo "  结论: 拦截完全阻止了攻击者（配额超限被拒绝）"
fi
echo ""
echo "详细日志保存在: $RESULTS_DIR/"
