#!/bin/bash
# Real MR Deregistration Abuse Experiment
# Compare Victim bandwidth with and without interception

set -e

RESULTS_DIR="/home/why/rdma_intercept_ldpreload/experiments/exp_mr_dereg/results"
SCRIPT_DIR="/home/why/rdma_intercept_ldpreload/experiments/exp_mr_dereg"
DURATION=30

# Machine config
LOCAL_MGMT="10.157.195.92"
LOCAL_RDMA="192.10.10.104"
REMOTE_MGMT="10.157.195.93"
REMOTE_RDMA="192.10.10.105"

mkdir -p $RESULTS_DIR

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo ""
    echo "========================================"
    echo "$1"
    echo "========================================"
    echo ""
}

# Cleanup function
cleanup() {
    log_info "Cleaning up..."
    pkill -f "ib_write_bw" 2>/dev/null || true
    pkill -f "victim_monitor" 2>/dev/null || true
    pkill -f "attacker" 2>/dev/null || true
    ssh -o StrictHostKeyChecking=no $REMOTE_MGMT "pkill -f ib_write_bw 2>/dev/null || true" 2>/dev/null || true
    sleep 2
}

# Run remote command
run_remote() {
    ssh -o StrictHostKeyChecking=no $REMOTE_MGMT "$1"
}

# ============================================
# Experiment 1: Without Interception
# ============================================
run_experiment_no_intercept() {
    print_header "Experiment 1: WITHOUT Interception (Victim will be affected)"
    
    cleanup
    
    # Ensure no LD_PRELOAD is set
    unset LD_PRELOAD
    export LD_PRELOAD=""
    
    log_info "Step 1: Start Victim Server on remote..."
    run_remote "ib_write_bw -d mlx5_0 -x 2 -s 1048576 -q 8 -D 60 --report_gbits -p 20000 > /tmp/victim_server_no_intercept.log 2>&1 &"
    sleep 3
    
    log_info "Step 2: Start Victim Monitor (Client)..."
    $SCRIPT_DIR/src/victim_monitor $DURATION $RESULTS_DIR/victim_no_intercept.csv $REMOTE_RDMA &
    VICTIM_PID=$!
    
    log_info "Step 3: Wait 5s for baseline measurement..."
    sleep 5
    
    log_info "Step 4: Start Attacker (MR deregistration abuse)..."
    $SCRIPT_DIR/src/attacker --delay=0 --duration=25000 --num-mrs=50 --batch-size=10 > $RESULTS_DIR/attacker_no_intercept.log 2>&1 &
    ATTACKER_PID=$!
    
    log_info "Step 5: Wait for experiment to complete (${DURATION}s)..."
    wait $VICTIM_PID
    wait $ATTACKER_PID 2>/dev/null || true
    
    log_info "Step 6: Cleanup..."
    cleanup
    
    log_info "Experiment 1 complete!"
    echo ""
}

# ============================================
# Experiment 2: With Interception
# ============================================
run_experiment_with_intercept() {
    print_header "Experiment 2: WITH Interception (Victim should be protected)"
    
    cleanup
    
    log_info "Step 1: Setup tenant quotas..."
    # Create tenant 10 for Victim with high quota
    $SCRIPT_DIR/../../build/tenant_manager --create 10 --name "Victim" --quota 100,100,1073741824 2>/dev/null || true
    # Create tenant 20 for Attacker with limited MR quota (20)
    $SCRIPT_DIR/../../build/tenant_manager --create 20 --name "Attacker" --quota 100,20,1073741824 2>/dev/null || true
    
    log_info "Step 2: Start Victim Server on remote (with intercept)..."
    run_remote "export LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so; export RDMA_INTERCEPT_ENABLE=1; export RDMA_TENANT_ID=10; ib_write_bw -d mlx5_0 -x 2 -s 1048576 -q 8 -D 60 --report_gbits -p 20001 > /tmp/victim_server_intercept.log 2>&1 &"
    sleep 3
    
    log_info "Step 3: Start Victim Monitor (Client with intercept)..."
    export LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so
    export RDMA_INTERCEPT_ENABLE=1
    export RDMA_TENANT_ID=10
    $SCRIPT_DIR/src/victim_monitor $DURATION $RESULTS_DIR/victim_with_intercept.csv $REMOTE_RDMA &
    VICTIM_PID=$!
    unset LD_PRELOAD RDMA_INTERCEPT_ENABLE RDMA_TENANT_ID
    
    log_info "Step 4: Wait 5s for baseline measurement..."
    sleep 5
    
    log_info "Step 5: Start Attacker (with intercept, limited quota)..."
    export LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so
    export RDMA_INTERCEPT_ENABLE=1
    export RDMA_TENANT_ID=20
    $SCRIPT_DIR/src/attacker --delay=0 --duration=25000 --num-mrs=50 --batch-size=10 > $RESULTS_DIR/attacker_with_intercept.log 2>&1 &
    ATTACKER_PID=$!
    unset LD_PRELOAD RDMA_INTERCEPT_ENABLE RDMA_TENANT_ID
    
    log_info "Step 6: Wait for experiment to complete (${DURATION}s)..."
    wait $VICTIM_PID
    wait $ATTACKER_PID 2>/dev/null || true
    
    log_info "Step 7: Cleanup..."
    cleanup
    
    log_info "Experiment 2 complete!"
    echo ""
}

# ============================================
# Main
# ============================================
main() {
    print_header "MR Deregistration Abuse Attack - Real Experiment"
    echo "This experiment will run two scenarios:"
    echo "  1. WITHOUT interception - Victim bandwidth should drop"
    echo "  2. WITH interception - Victim bandwidth should be stable"
    echo ""
    echo "Duration: ${DURATION} seconds per experiment"
    echo "Results will be saved to: $RESULTS_DIR"
    echo ""
    read -p "Press Enter to start..."
    
    # Check if attacker exists
    if [ ! -f "$SCRIPT_DIR/src/attacker" ]; then
        log_error "Attacker not found. Please compile it first."
        exit 1
    fi
    
    # Run experiments
    run_experiment_no_intercept
    sleep 3
    run_experiment_with_intercept
    
    print_header "All Experiments Complete!"
    log_info "Results saved to:"
    log_info "  - $RESULTS_DIR/victim_no_intercept.csv"
    log_info "  - $RESULTS_DIR/victim_with_intercept.csv"
    log_info "  - $RESULTS_DIR/attacker_no_intercept.log"
    log_info "  - $RESULTS_DIR/attacker_with_intercept.log"
    echo ""
    log_info "Next step: Run analysis/plot_real_results.py to generate charts"
}

# Handle Ctrl+C
trap cleanup EXIT

main "$@"
