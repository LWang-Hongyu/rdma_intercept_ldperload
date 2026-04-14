#!/bin/bash
# EXP-2: 多租户隔离验证 (启用QP控制)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results/exp2"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "=========================================="
echo "EXP-2: 多租户隔离验证"
echo "=========================================="
echo ""

mkdir -p "$RESULTS_DIR"

# 编译测试程序
if [ ! -f "$SCRIPT_DIR/exp2_multi_tenant_isolation" ]; then
    echo "[Build] Compiling exp2_multi_tenant_isolation..."
    gcc -O2 -o "$SCRIPT_DIR/exp2_multi_tenant_isolation" \
        "$SCRIPT_DIR/src/exp2_multi_tenant_isolation.c" -libverbs -lpthread || {
        echo "[ERROR] Failed to compile"
        exit 1
    }
fi

# 创建租户工具
if [ ! -f "$SCRIPT_DIR/create_tenant" ]; then
    echo "[Build] Compiling create_tenant..."
    gcc -o "$SCRIPT_DIR/create_tenant" "$SCRIPT_DIR/../exp9_mr_isolation/create_tenant.c" \
        -I"$PROJECT_DIR/src" -I"$PROJECT_DIR/src/shm" \
        -L"$PROJECT_DIR/build" -ltenant_shared_memory -lshared_memory -lrt -lpthread || {
        echo "[ERROR] Failed to compile create_tenant"
        exit 1
    }
fi

# 设置环境变量启用QP控制
export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1
export RDMA_INTERCEPT_ENABLE=1

# 使用新的租户ID（避免冲突）
TENANT_1=7
TENANT_A=13
TENANT_B=14
TENANT_HEAVY=33
TENANT_LIGHT=34

# 场景1: 单租户基准测试
echo "=========================================="
echo "场景1: 单租户基准测试"
echo "=========================================="
echo "创建租户$TENANT_1 (配额=50 QP)..."
"$SCRIPT_DIR/create_tenant" $TENANT_1 50 50 1073741824 "Tenant1_Single" 2>/dev/null || echo "租户已存在"

echo "运行单租户测试..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" RDMA_TENANT_ID=$TENANT_1 \
    "$SCRIPT_DIR/exp2_multi_tenant_isolation" -t $TENANT_1 -q 50 -e 10 \
    -o "$RESULTS_DIR/scene1_single.txt"

echo ""
echo "=========================================="
echo "场景2: 两租户公平性测试"
echo "=========================================="
echo "创建租户$TENANT_A (配额=20 QP)..."
"$SCRIPT_DIR/create_tenant" $TENANT_A 20 20 1073741824 "TenantA" 2>/dev/null || echo "租户已存在"

echo "创建租户$TENANT_B (配额=20 QP)..."
"$SCRIPT_DIR/create_tenant" $TENANT_B 20 20 1073741824 "TenantB" 2>/dev/null || echo "租户已存在"

echo "运行租户A测试..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" RDMA_TENANT_ID=$TENANT_A \
    "$SCRIPT_DIR/exp2_multi_tenant_isolation" -t $TENANT_A -q 20 -e 10 \
    -o "$RESULTS_DIR/scene2_tenantA.txt" &
PID_A=$!

echo "运行租户B测试..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" RDMA_TENANT_ID=$TENANT_B \
    "$SCRIPT_DIR/exp2_multi_tenant_isolation" -t $TENANT_B -q 20 -e 10 \
    -o "$RESULTS_DIR/scene2_tenantB.txt" &
PID_B=$!

echo "等待两个租户完成..."
wait $PID_A
wait $PID_B

echo ""
echo "=========================================="
echo "场景3: 多租户干扰测试"
echo "=========================================="
echo "创建重负载租户 (配额=50 QP)..."
"$SCRIPT_DIR/create_tenant" $TENANT_HEAVY 50 50 1073741824 "HeavyTenant" 2>/dev/null || echo "租户已存在"

echo "创建轻负载租户 (配额=10 QP)..."
"$SCRIPT_DIR/create_tenant" $TENANT_LIGHT 10 10 1073741824 "LightTenant" 2>/dev/null || echo "租户已存在"

echo "先测试轻负载租户基线..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" RDMA_TENANT_ID=$TENANT_LIGHT \
    "$SCRIPT_DIR/exp2_multi_tenant_isolation" -t $TENANT_LIGHT -q 10 -e 5 \
    -o "$RESULTS_DIR/scene3_light_baseline.txt"

echo "同时运行重负载和轻负载租户..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" RDMA_TENANT_ID=$TENANT_HEAVY \
    "$SCRIPT_DIR/exp2_multi_tenant_isolation" -t $TENANT_HEAVY -q 50 -e 20 \
    -o "$RESULTS_DIR/scene3_heavy.txt" &
PID_HEAVY=$!

sleep 0.5

LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" RDMA_TENANT_ID=$TENANT_LIGHT \
    "$SCRIPT_DIR/exp2_multi_tenant_isolation" -t $TENANT_LIGHT -q 10 -e 5 \
    -o "$RESULTS_DIR/scene3_light_with_heavy.txt" &
PID_LIGHT=$!

wait $PID_HEAVY
wait $PID_LIGHT

echo ""
echo "=========================================="
echo "实验完成！"
echo "结果保存在: $RESULTS_DIR/"
echo "=========================================="

# 显示结果摘要
echo ""
echo "结果摘要:"
echo "---------"
for f in "$RESULTS_DIR"/*.txt; do
    if [ -f "$f" ]; then
        echo "$(basename $f):"
        grep -E "(TENANT_ID|QUOTA|CREATED|DENIED)" "$f" | head -4
        echo ""
    fi
done
