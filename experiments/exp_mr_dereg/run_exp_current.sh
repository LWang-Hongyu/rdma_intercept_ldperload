#!/bin/bash
# EXP-MR-DEREG: MR注销滥用攻击实验（当前机器配置）
# 本机: 10.157.195.92 (RDMA: 192.10.10.104)
# 对端: 10.157.195.93 (RDMA: 192.10.10.105)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
RESULTS_DIR="$SCRIPT_DIR/results"

LOCAL_MGMT="10.157.195.92"
LOCAL_RDMA="192.10.10.104"
REMOTE_MGMT="10.157.195.93"
REMOTE_RDMA="192.10.10.105"

GID_INDEX="2"
DEV="mlx5_0"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

print_header() {
    echo ""
    echo "========================================"
    echo "$1"
    echo "========================================"
}

# 函数: 在远程机器上运行命令
run_remote() {
    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "$REMOTE_MGMT" "$@"
}

cleanup() {
    log_warn "Cleaning up..."
    run_remote "pkill -f 'ib_write_bw' || true" 2>/dev/null || true
    pkill -f "attacker" 2>/dev/null || true
}
trap cleanup INT TERM EXIT

mkdir -p "$RESULTS_DIR"

print_header "EXP-MR-DEREG: MR注销滥用攻击实验"
echo "网络配置:"
echo "  Local:  $LOCAL_RDMA ($LOCAL_MGMT)"
echo "  Remote: $REMOTE_RDMA ($REMOTE_MGMT)"
echo ""

# ========================================
# 场景A: 无拦截（攻击成功）
# ========================================
run_baseline() {
    print_header "场景A: 无拦截（攻击成功）"
    
    log_info "Step 1: 在对端启动Victim Server..."
    run_remote "ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 8 -D 60 --report_gbits -p 20000" > /tmp/victim_server.log 2>&1 &
    sleep 3
    
    log_info "Step 2: 启动Victim Client..."
    # 使用ib_write_bw作为Victim，记录带宽
    ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 8 -D 35 --report_gbits -p 20000 $REMOTE_RDMA 2>&1 | tee /tmp/victim_bw.log &
    VICTIM_PID=$!
    
    log_info "Step 3: 等待5秒建立基线..."
    sleep 5
    
    log_info "Step 4: 启动Attacker（MR注销滥用攻击）..."
    "$SCRIPT_DIR/attacker" --delay=0 --duration=25000 --num-mrs=50 --batch-size=10 &
    ATTACKER_PID=$!
    
    log_info "Step 5: 等待攻击完成（25秒）..."
    wait $ATTACKER_PID
    
    log_info "Step 6: 收集结果..."
    sleep 5
    kill $VICTIM_PID 2>/dev/null || true
    
    # 解析带宽数据
    grep -E "^[0-9]+[[:space:]]+[0-9]+" /tmp/victim_bw.log | awk '{print $4}' > "$RESULTS_DIR/baseline_bw.txt"
    
    log_success "场景A完成"
    echo ""
}

# ========================================
# 场景B: 有拦截（攻击失败）
# ========================================
run_protected() {
    print_header "场景B: 有拦截（攻击失败）"
    
    log_info "Step 1: 在对端启动Victim Server..."
    run_remote "ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 8 -D 60 --report_gbits -p 20001" > /tmp/victim_server2.log 2>&1 &
    sleep 3
    
    log_info "Step 2: 启动Victim Client（带LD_PRELOAD拦截）..."
    LD_PRELOAD=$PROJECT_DIR/build/librdma_intercept.so RDMA_INTERCEPT_ENABLE=1 \
        ib_write_bw -d $DEV -x $GID_INDEX -s 1048576 -q 8 -D 35 --report_gbits -p 20001 $REMOTE_RDMA 2>&1 | tee /tmp/victim_bw_protected.log &
    VICTIM_PID=$!
    
    log_info "Step 3: 等待5秒建立基线..."
    sleep 5
    
    log_info "Step 4: 启动Attacker（带LD_PRELOAD拦截）..."
    LD_PRELOAD=$PROJECT_DIR/build/librdma_intercept.so RDMA_INTERCEPT_ENABLE=1 \
        "$SCRIPT_DIR/attacker" --delay=0 --duration=25000 --num-mrs=50 --batch-size=10 &
    ATTACKER_PID=$!
    
    log_info "Step 5: 等待攻击完成（25秒）..."
    wait $ATTACKER_PID
    
    log_info "Step 6: 收集结果..."
    sleep 5
    kill $VICTIM_PID 2>/dev/null || true
    
    # 解析带宽数据
    grep -E "^[0-9]+[[:space:]]+[0-9]+" /tmp/victim_bw_protected.log | awk '{print $4}' > "$RESULTS_DIR/protected_bw.txt"
    
    log_success "场景B完成"
    echo ""
}

# ========================================
# 分析结果
# ========================================
analyze_results() {
    print_header "实验结果分析"
    
    python3 << 'EOF'
import os

results_dir = "/home/why/rdma_intercept_ldpreload/experiments/exp_mr_dereg/results"

def read_bw_file(filename):
    try:
        with open(filename, 'r') as f:
            values = [float(line.strip()) for line in f if line.strip()]
        return values
    except:
        return []

baseline = read_bw_file(f"{results_dir}/baseline_bw.txt")
protected = read_bw_file(f"{results_dir}/protected_bw.txt")

print("="*60)
print("EXP-MR-DEREG: MR注销滥用攻击实验结果")
print("="*60)

if baseline:
    # 假设前5个样本是基线（5秒），后面是攻击阶段
    baseline_phase = baseline[:5] if len(baseline) >= 5 else baseline
    attack_phase = baseline[5:] if len(baseline) > 5 else []
    
    baseline_avg = sum(baseline_phase) / len(baseline_phase) if baseline_phase else 0
    attack_avg = sum(attack_phase) / len(attack_phase) if attack_phase else 0
    
    print(f"\n场景A: 无拦截")
    print(f"  基线带宽:   {baseline_avg:.2f} Gbps")
    print(f"  攻击带宽:   {attack_avg:.2f} Gbps")
    if baseline_avg > 0:
        degradation = (baseline_avg - attack_avg) / baseline_avg * 100
        print(f"  性能下降:   {degradation:.1f}%")
        if degradation > 30:
            print(f"  状态:       ❌ 易受攻击 (下降 > 30%)")
        else:
            print(f"  状态:       ⚠️  部分受影响")

if protected:
    baseline_phase_p = protected[:5] if len(protected) >= 5 else protected
    attack_phase_p = protected[5:] if len(protected) > 5 else []
    
    baseline_avg_p = sum(baseline_phase_p) / len(baseline_phase_p) if baseline_phase_p else 0
    attack_avg_p = sum(attack_phase_p) / len(attack_phase_p) if attack_phase_p else 0
    
    print(f"\n场景B: 有拦截")
    print(f"  基线带宽:   {baseline_avg_p:.2f} Gbps")
    print(f"  攻击带宽:   {attack_avg_p:.2f} Gbps")
    if baseline_avg_p > 0:
        degradation_p = (baseline_avg_p - attack_avg_p) / baseline_avg_p * 100
        print(f"  性能下降:   {degradation_p:.1f}%")
        if degradation_p < 10:
            print(f"  状态:       ✅ 受到保护 (下降 < 10%)")
        elif degradation_p < 30:
            print(f"  状态:       ⚠️  部分保护")
        else:
            print(f"  状态:       ❌ 保护不足")

print("\n" + "="*60)
EOF
}

# ========================================
# 主程序
# ========================================
SCENARIO=${1:-all}

case "$SCENARIO" in
    baseline)
        run_baseline
        ;;
    protected)
        run_protected
        ;;
    all)
        run_baseline
        sleep 5
        run_protected
        analyze_results
        ;;
    *)
        echo "用法: $0 [baseline|protected|all]"
        exit 1
        ;;
esac

print_header "实验完成"
log_info "结果保存在: $RESULTS_DIR"
