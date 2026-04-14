#!/bin/bash
# EXP-3 租户设置脚本

set -e

TENANT_MGR="./build/tenant_manager"
NUM_TENANTS=${1:-5}
QP_PER_TENANT=${2:-3}

echo "=== EXP-3: 设置 $NUM_TENANTS 个租户 ==="

# 清理现有租户（可选）
echo "[1/2] 清理现有测试租户..."
for i in $(seq 100 119); do
    $TENANT_MGR --delete $i 2>/dev/null || true
done
sleep 1

# 创建租户
echo "[2/2] 创建 $NUM_TENANTS 个租户（每个配额 $QP_PER_TENANT QP）..."
for i in $(seq 0 $((NUM_TENANTS-1))); do
    tenant_id=$((100 + i))
    $TENANT_MGR --create $tenant_id --name "EXP3_T$i" --quota $QP_PER_TENANT,100,100 2>/dev/null || true
    echo -n "."
done
echo ""

echo "租户列表:"
$TENANT_MGR --list 2>&1 | grep -E "(Active Tenants|ID|100|101|102|103|104|105|106|107|108|109|110)"

echo ""
echo "✓ 租户设置完成"
