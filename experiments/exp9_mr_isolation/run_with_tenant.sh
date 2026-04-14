#!/bin/bash
# EXP-9: MR资源隔离验证测试 - 使用租户级别限制
# 先创建租户，再运行测试

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "=========================================="
echo "EXP-9: MR资源隔离验证 (租户级别)"
echo "=========================================="
echo ""

mkdir -p "$RESULTS_DIR"

# 编译测试程序
if [ ! -f "$SCRIPT_DIR/exp9_mr_isolation" ]; then
    echo "[Build] Compiling exp9_mr_isolation..."
    gcc -O2 -o "$SCRIPT_DIR/exp9_mr_isolation" \
        "$SCRIPT_DIR/src/exp9_mr_isolation.c" -libverbs -lpthread || {
        echo "[ERROR] Failed to compile"
        exit 1
    }
fi

# 启动租户管理守护进程（如果未运行）
echo "[Setup] 启动租户管理守护进程..."
if ! pgrep -x "tenant_manager_daemon" > /dev/null; then
    sudo "$PROJECT_DIR/build/tenant_manager_daemon" --daemon --foreground &
    DAEMON_PID=$!
    sleep 2
    echo "[Setup] 守护进程已启动 (PID: $DAEMON_PID)"
else
    echo "[Setup] 守护进程已在运行"
fi

# 创建租户10，设置MR限制为10
echo "[Setup] 创建租户10，MR限制=10..."
sudo "$PROJECT_DIR/build/tenant_manager_client" create 10 100 10 1073741824 "TestTenantForMR" || {
    echo "[Warning] 租户创建可能已存在，继续执行..."
}

echo ""
echo "[Test] 场景1: 无拦截（基线）"
unset LD_PRELOAD
"$SCRIPT_DIR/exp9_mr_isolation" -t 10 -n 50 -s 1048576 -o "$RESULTS_DIR/baseline.txt"

echo ""
echo "[Test] 场景2: 有拦截，租户10 MR限制=10"
export LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so"
export RDMA_INTERCEPT_ENABLE=1
export RDMA_TENANT_ID=10
"$SCRIPT_DIR/exp9_mr_isolation" -t 10 -n 50 -s 1048576 -o "$RESULTS_DIR/with_tenant_limit.txt"
unset LD_PRELOAD

echo ""
echo "=========================================="
echo "实验完成！"
echo "结果保存在:"
echo "  - $RESULTS_DIR/baseline.txt"
echo "  - $RESULTS_DIR/with_tenant_limit.txt"
echo "=========================================="
