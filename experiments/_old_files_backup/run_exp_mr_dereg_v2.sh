#!/bin/bash
# EXP-MR-DEREG-V2: 自动化MR注销滥用攻击实验 (简化版)
# 
# 用法: ./run_exp_mr_dereg_v2.sh [baseline|protected|all]
#   baseline  - 运行场景A: 无拦截
#   protected - 运行场景B: 有拦截
#   all       - 运行两个场景（默认）

set -e

LOCAL_MGMT="10.157.197.53"
REMOTE_MGMT="10.157.197.51"
REMOTE_RDMA="192.168.106.2"
EXPERIMENTS_DIR="$HOME/rdma_intercept_ldpreload/experiments"
PROJECT_DIR="$HOME/rdma_intercept_ldpreload"

# 默认运行所有场景
SCENARIO=${1:-all}

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

cleanup() {
    log_warn "Cleaning up..."
    ssh -o ConnectTimeout=3 why@$REMOTE_MGMT "pkill -f 'ib_write_bw' || true" 2>/dev/null || true
    pkill -f "exp_mr_dereg_attacker" 2>/dev/null || true
    pkill -f "victim_bw_monitor" 2>/dev/null || true
    sudo pkill -f "librdma_intercept_daemon" 2>/dev/null || true
}
trap cleanup INT TERM EXIT

check_prerequisites() {
    print_header "Step 0: Prerequisites Check"
    
    # 检查ib_write_bw
    if ! command -v ib_write_bw &> /dev/null; then
        log_error "ib_write_bw not found. Install with: sudo apt-get install perftest"
        exit 1
    fi
    log_success "ib_write_bw available"
    
    # 检查SSH连接
    if ! ssh -o ConnectTimeout=5 why@$REMOTE_MGMT "hostname" > /dev/null 2>&1; then
        log_error "Cannot connect to remote host via SSH"
        exit 1
    fi
    log_success "SSH connection to remote OK"
    
    # 检查Python3
    if ! python3 -c "import matplotlib" 2>/dev/null; then
        log_warn "matplotlib not installed. Will generate CSV but not plots."
        log_info "Install with: pip3 install matplotlib"
    fi
    
    # 编译攻击程序
    if [ ! -f "$EXPERIMENTS_DIR/exp_mr_dereg_attacker" ]; then
        log_info "Compiling attacker program..."
        gcc -O2 -o $EXPERIMENTS_DIR/exp_mr_dereg_attacker \
            $EXPERIMENTS_DIR/exp_mr_dereg_attacker.c -libverbs -lpthread || {
            log_error "Failed to compile attacker"
            exit 1
        }
    fi
    log_success "Attacker ready"
}

run_baseline() {
    print_header "Scenario A: WITHOUT Protection"
    log_info "Demonstrating how MR deregistration abuse degrades Victim bandwidth"
    
    cleanup
    sleep 1
    
    # 步骤1: 在远端启动服务器
    log_info "[Step 1/4] Starting ib_write_bw server on remote..."
    ssh why@$REMOTE_MGMT "ib_write_bw -d mlx5_0 --report_gbits" > /tmp/server.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    
    # 步骤2: 启动Victim客户端（运行30秒）
    log_info "[Step 2/4] Starting Victim bandwidth monitor..."
    python3 $EXPERIMENTS_DIR/victim_bw_monitor.py \
        --mode=client \
        --server=$REMOTE_RDMA \
        --duration=30 \
        --output=$EXPERIMENTS_DIR/victim_no_protection.csv &
    VICTIM_PID=$!
    
    sleep 5  # 等待基线建立
    
    if ! ps -p $VICTIM_PID > /dev/null 2>&1; then
        log_error "Victim monitor exited early"
        exit 1
    fi
    
    # 步骤3: 启动Attacker
    log_info "[Step 3/4] Starting Attacker..."
    $EXPERIMENTS_DIR/exp_mr_dereg_attacker \
        --delay=0 \
        --duration=25000 \
        --num_mrs=50 \
        --batch_size=10 &
    ATTACKER_PID=$!
    
    # 步骤4: 等待完成
    log_info "[Step 4/4] Running experiment (30 seconds)..."
    
    for i in {1..30}; do
        sleep 1
        if [ $((i % 5)) -eq 0 ]; then
            log_info "Progress: ${i}/30 seconds"
        fi
    done
    
    wait $VICTIM_PID 2>/dev/null || true
    kill $ATTACKER_PID 2>/dev/null || true
    
    log_success "Experiment completed!"
    
    # 分析结果
    if [ -f "$EXPERIMENTS_DIR/victim_no_protection.csv" ]; then
        python3 $EXPERIMENTS_DIR/analyze_bandwidth.py \
            $EXPERIMENTS_DIR/victim_no_protection.csv \
            $EXPERIMENTS_DIR/victim_no_protection.png 2>/dev/null || {
            log_warn "Failed to generate plot, CSV file is available"
        }
    fi
}

run_protected() {
    print_header "Scenario B: WITH Protection (quota=20)"
    log_info "Demonstrating how interception system prevents bandwidth degradation"
    
    cleanup
    sleep 1
    
    # 检查拦截库
    if [ ! -f "$PROJECT_DIR/build/librdma_intercept.so" ]; then
        log_error "Intercept library not found. Build first: cd $PROJECT_DIR && make"
        exit 1
    fi
    
    # 步骤1: 启动守护进程
    log_info "[Step 1/5] Starting intercept daemon..."
    cd $PROJECT_DIR
    sudo ./build/librdma_intercept_daemon --quota-mr=20 > /tmp/daemon.log 2>&1 &
    DAEMON_PID=$!
    sleep 2
    
    # 步骤2: 在远端启动服务器
    log_info "[Step 2/5] Starting ib_write_bw server on remote..."
    ssh why@$REMOTE_MGMT "ib_write_bw -d mlx5_0 --report_gbits" > /tmp/server.log 2>&1 &
    sleep 2
    
    # 步骤3: 使用LD_PRELOAD启动Victim
    log_info "[Step 3/5] Starting Victim bandwidth monitor (with LD_PRELOAD)..."
    cd $PROJECT_DIR
    LD_PRELOAD=$PROJECT_DIR/build/librdma_intercept.so \
        python3 $EXPERIMENTS_DIR/victim_bw_monitor.py \
        --mode=client \
        --server=$REMOTE_RDMA \
        --duration=30 \
        --output=$EXPERIMENTS_DIR/victim_with_protection.csv &
    VICTIM_PID=$!
    
    sleep 5
    
    if ! ps -p $VICTIM_PID > /dev/null 2>&1; then
        log_error "Victim monitor exited early"
        sudo kill $DAEMON_PID 2>/dev/null || true
        exit 1
    fi
    
    # 步骤4: 使用LD_PRELOAD启动Attacker
    log_info "[Step 4/5] Starting Attacker (with LD_PRELOAD, quota=20)..."
    LD_PRELOAD=$PROJECT_DIR/build/librdma_intercept.so \
        $EXPERIMENTS_DIR/exp_mr_dereg_attacker \
        --delay=0 \
        --duration=25000 \
        --num_mrs=50 \
        --batch_size=10 &
    ATTACKER_PID=$!
    
    # 步骤5: 等待完成
    log_info "[Step 5/5] Running experiment (30 seconds)..."
    
    for i in {1..30}; do
        sleep 1
        if [ $((i % 5)) -eq 0 ]; then
            log_info "Progress: ${i}/30 seconds"
        fi
    done
    
    wait $VICTIM_PID 2>/dev/null || true
    kill $ATTACKER_PID 2>/dev/null || true
    sudo kill $DAEMON_PID 2>/dev/null || true
    
    log_success "Experiment completed!"
    
    # 分析结果
    if [ -f "$EXPERIMENTS_DIR/victim_with_protection.csv" ]; then
        python3 $EXPERIMENTS_DIR/analyze_bandwidth.py \
            $EXPERIMENTS_DIR/victim_with_protection.csv \
            $EXPERIMENTS_DIR/victim_with_protection.png 2>/dev/null || {
            log_warn "Failed to generate plot, CSV file is available"
        }
    fi
}

generate_comparison() {
    print_header "Comparison Summary"
    
    python3 << 'EOF'
import csv
import os
import sys

def get_stats(filename):
    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            data = list(reader)
        
        baseline = [float(r['bandwidth_gbps']) for r in data if int(r['phase']) == 0]
        attack = [float(r['bandwidth_gbps']) for r in data if int(r['phase']) == 1]
        
        baseline_avg = sum(baseline) / len(baseline) if baseline else 0
        attack_avg = sum(attack) / len(attack) if attack else 0
        degradation = ((baseline_avg - attack_avg) / baseline_avg * 100) if baseline_avg > 0 else 0
        
        return baseline_avg, attack_avg, degradation
    except Exception as e:
        print(f"Error reading {filename}: {e}")
        return 0, 0, 0

exp_dir = os.path.expanduser("~/rdma_intercept_ldpreload/experiments")
no_prot = get_stats(os.path.join(exp_dir, "victim_no_protection.csv"))
prot = get_stats(os.path.join(exp_dir, "victim_with_protection.csv"))

print("\n" + "="*70)
print(" "*20 + "COMPARISON SUMMARY")
print("="*70)
print(f"\n{'Metric':<30} {'No Protection':>18} {'With Protection':>18}")
print("-"*70)
print(f"{'Baseline Bandwidth':<30} {no_prot[0]:>16.2f}G {prot[0]:>16.2f}G")
print(f"{'Attack Phase Bandwidth':<30} {no_prot[1]:>16.2f}G {prot[1]:>16.2f}G")
print(f"{'Bandwidth Degradation':<30} {no_prot[2]:>16.1f}% {prot[2]:>16.1f}%")
print("-"*70)

if no_prot[2] > 30 and prot[2] < 15:
    print(f"{'Protection Status':<30} {'❌ VULNERABLE':>18} {'✅ PROTECTED':>18}")
    print("\n" + "="*70)
    print("Conclusion: Interception system SUCCESSFULLY prevents the attack!")
    print("="*70)
    sys.exit(0)
else:
    print(f"{'Protection Status':<30} {'❓ UNCLEAR':>18} {'❓ UNCLEAR':>18}")
    print("\n" + "="*70)
    print("Note: Results may vary. Check individual CSV files for details.")
    print("="*70)
    sys.exit(1)
EOF
}

main() {
    echo "========================================"
    echo "EXP-MR-DEREG-V2: MR Deregistration Abuse"
    echo "   Attack Impact on Victim Bandwidth"
    echo "========================================"
    echo "Local:  $LOCAL_MGMT (RDMA: 192.168.108.2)"
    echo "Remote: $REMOTE_MGMT (RDMA: $REMOTE_RDMA)"
    echo "========================================"
    
    # 检查参数
    if [ "$SCENARIO" != "baseline" ] && [ "$SCENARIO" != "protected" ] && [ "$SCENARIO" != "all" ]; then
        echo "Usage: $0 [baseline|protected|all]"
        echo ""
        echo "Scenarios:"
        echo "  baseline  - Run without protection (shows attack effectiveness)"
        echo "  protected - Run with protection (shows defense effectiveness)"
        echo "  all       - Run both scenarios (default)"
        exit 1
    fi
    
    check_prerequisites
    
    # 运行场景
    if [ "$SCENARIO" == "baseline" ] || [ "$SCENARIO" == "all" ]; then
        run_baseline
    fi
    
    if [ "$SCENARIO" == "protected" ] || [ "$SCENARIO" == "all" ]; then
        if [ "$SCENARIO" == "all" ]; then
            echo ""
            read -p "Press Enter to continue to Scenario B (with protection)..."
        fi
        run_protected
    fi
    
    # 生成对比报告
    if [ "$SCENARIO" == "all" ]; then
        generate_comparison
    fi
    
    print_header "Experiment Completed!"
    log_info "Result files:"
    [ -f "$EXPERIMENTS_DIR/victim_no_protection.csv" ] && \
        log_info "  - No Protection:  $EXPERIMENTS_DIR/victim_no_protection.{csv,png}"
    [ -f "$EXPERIMENTS_DIR/victim_with_protection.csv" ] && \
        log_info "  - With Protection: $EXPERIMENTS_DIR/victim_with_protection.{csv,png}"
}

main
