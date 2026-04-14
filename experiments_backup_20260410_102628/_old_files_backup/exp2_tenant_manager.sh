#!/bin/bash
# EXP-2 租户管理脚本

set -e

TENANT_MGR="./build/tenant_manager"
SHM_INIT="./build/collector_server_shm"

echo "=== EXP-2 租户环境初始化 ==="

# 检查可执行文件
if [ ! -f "$TENANT_MGR" ]; then
    echo "错误: 未找到tenant_manager，请先编译项目"
    exit 1
fi

# 1. 初始化共享内存
echo "[1/3] 初始化共享内存..."
if [ -f /dev/shm/rdma_intercept_shm ]; then
    echo "  共享内存已存在，跳过初始化"
else
    echo "  注意: 共享内存不存在，tenant_manager会自动创建"
fi

# 2. 创建租户1 (配额3个QP)
echo "[2/3] 创建租户100 (配额: 3 QP)..."
$TENANT_MGR create 100 --max-qp 3 2>/dev/null || echo "  租户100已存在或创建失败"

# 3. 创建租户2 (配额3个QP)
echo "[3/3] 创建租户200 (配额: 3 QP)..."
$TENANT_MGR create 200 --max-qp 3 2>/dev/null || echo "  租户200已存在或创建失败"

# 显示租户状态
echo ""
echo "当前租户状态:"
$TENANT_MGR list 2>/dev/null || echo "  无法获取租户列表"

echo ""
echo "✓ 租户环境初始化完成"
