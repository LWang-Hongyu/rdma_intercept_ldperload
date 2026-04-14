#!/bin/bash
# MR Deregistration Abuse Attack - Comparison Test
# 
# This script compares the effect of MR deregistration abuse attack
# with and without RDMA interception protection.
#
# Based on: "Understanding RDMA Microarchitecture Resources for Performance Isolation" (NSDI'23)
# Attack: Rapid MR deregister/reregister causing MTT cache thrashing

set -e

# Configuration
TEST_DURATION=30
NUM_MRS=100
MR_SIZE=$((4 * 1024 * 1024))  # 4MB
BATCH_SIZE=10
INTERCEPT_LIB="/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so"
RESULTS_DIR="/home/why/rdma_intercept_ldpreload/paper_results/exp_mr_dereg_results"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "MR Deregistration Abuse Attack Test"
echo "Comparison: With vs Without Interception"
echo "========================================"
echo ""

# Create results directory
mkdir -p $RESULTS_DIR

# Function to print section header
print_section() {
    echo ""
    echo -e "${YELLOW}$1${NC}"
    echo "========================================"
}

# Function to run victim workload
run_victim() {
    local output_file=$1
    local duration=$2
    
    # Simple victim: Repeatedly register/deregister a single MR
    # This measures the impact of attacker's MTT cache pollution
    
    echo "[Victim] Starting MR operation workload..."
    
    cat > /tmp/victim_mr_test.cpp << 'EOF'
#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/time.h>
#include <infiniband/verbs.h>

#define MR_SIZE (1 * 1024 * 1024)  // 1MB

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <duration_sec>" << std::endl;
        return 1;
    }
    
    int duration = atoi(argv[1]);
    
    // Setup
    int num_devices;
    struct ibv_device** dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list || num_devices == 0) {
        std::cerr << "No IB devices found" << std::endl;
        return 1;
    }
    
    struct ibv_context* ctx = ibv_open_device(dev_list[0]);
    if (!ctx) {
        std::cerr << "Failed to open device" << std::endl;
        return 1;
    }
    
    struct ibv_pd* pd = ibv_alloc_pd(ctx);
    if (!pd) {
        std::cerr << "Failed to allocate PD" << std::endl;
        return 1;
    }
    
    void* buffer = aligned_alloc(4096, MR_SIZE);
    if (!buffer) {
        std::cerr << "Failed to allocate buffer" << std::endl;
        return 1;
    }
    memset(buffer, 0, MR_SIZE);
    
    // Test loop
    struct timeval start, end;
    gettimeofday(&start, nullptr);
    
    std::vector<double> latencies;
    int count = 0;
    
    while (true) {
        gettimeofday(&end, nullptr);
        double elapsed = (end.tv_sec - start.tv_sec) + 
                        (end.tv_usec - start.tv_usec) / 1000000.0;
        if (elapsed >= duration) break;
        
        // Register MR
        struct timeval reg_start, reg_end;
        gettimeofday(&reg_start, nullptr);
        
        struct ibv_mr* mr = ibv_reg_mr(pd, buffer, MR_SIZE,
                                       IBV_ACCESS_LOCAL_WRITE | 
                                       IBV_ACCESS_REMOTE_READ | 
                                       IBV_ACCESS_REMOTE_WRITE);
        
        gettimeofday(&reg_end, nullptr);
        
        if (!mr) {
            std::cerr << "MR registration failed at iteration " << count << std::endl;
            break;
        }
        
        double reg_latency = (reg_end.tv_sec - reg_start.tv_sec) * 1000000 + 
                             (reg_end.tv_usec - reg_start.tv_usec);
        latencies.push_back(reg_latency);
        
        // Small delay to simulate real workload
        usleep(1000);  // 1ms
        
        // Deregister MR
        ibv_dereg_mr(mr);
        
        count++;
    }
    
    // Output statistics
    if (!latencies.empty()) {
        double sum = 0;
        double max_lat = 0;
        for (auto lat : latencies) {
            sum += lat;
            if (lat > max_lat) max_lat = lat;
        }
        double avg = sum / latencies.size();
        
        std::cout << "\n[Victim Results]" << std::endl;
        std::cout << "  Total MR operations: " << count << std::endl;
        std::cout << "  Avg registration latency: " << avg << " us" << std::endl;
        std::cout << "  Max registration latency: " << max_lat << " us" << std::endl;
        
        // Also output to file
        FILE* fp = fopen("/tmp/victim_result.txt", "w");
        if (fp) {
            fprintf(fp, "count %d\n", count);
            fprintf(fp, "avg_latency %.2f\n", avg);
            fprintf(fp, "max_latency %.2f\n", max_lat);
            fclose(fp);
        }
    }
    
    // Cleanup
    free(buffer);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    return 0;
}
EOF
    
    # Compile victim
    g++ -std=c++11 -O2 -o /tmp/victim_mr_test /tmp/victim_mr_test.cpp -libverbs
    
    # Run victim
    /tmp/victim_mr_test $duration 2>&1 | tee $output_file
}

# Function to run attacker
run_attacker() {
    local use_intercept=$1
    local duration=$2
    
    if [ "$use_intercept" == "true" ]; then
        echo -e "${GREEN}[Attacker with Interception]${NC} Starting MR dereg abuse attack..."
        export LD_PRELOAD=$INTERCEPT_LIB
        export RDMA_INTERCEPT_ENABLE=1
        export RDMA_TENANT_ID=20
    else
        echo -e "${RED}[Attacker without Interception]${NC} Starting MR dereg abuse attack..."
        unset LD_PRELOAD RDMA_INTERCEPT_ENABLE RDMA_TENANT_ID
    fi
    
    ./exp_mr_dereg_abuse -t $duration -n $NUM_MRS -b $BATCH_SIZE 2>&1 | tee $RESULTS_DIR/attacker_${use_intercept}.log
    
    unset LD_PRELOAD RDMA_INTERCEPT_ENABLE RDMA_TENANT_ID
}

# ============================================================
# TEST 1: WITHOUT INTERCEPTION
# ============================================================
print_section "TEST 1: Attack WITHOUT Interception Protection"

echo "[Setup] This test shows the effect of MR deregistration abuse"
echo "        when there is NO protection mechanism."
echo ""
echo "Expected: Attacker can freely abuse MR operations,"
echo "          potentially degrading Victim's performance."
echo ""

# Start victim in background
echo "[Step 1] Starting Victim workload..."
run_victim $RESULTS_DIR/victim_without_intercept.log $TEST_DURATION &
VICTIM_PID=$!
sleep 2

# Run attacker without interception
echo ""
echo "[Step 2] Starting Attacker (NO interception)..."
run_attacker "false" $TEST_DURATION &
ATTACKER_PID=$!

# Wait for completion
echo ""
echo "[Step 3] Waiting for test completion (${TEST_DURATION}s)..."
wait $ATTACKER_PID
wait $VICTIM_PID

echo ""
echo -e "${GREEN}Test 1 Complete${NC}"
cp /tmp/victim_result.txt $RESULTS_DIR/victim_stats_without_intercept.txt

# ============================================================
# TEST 2: WITH INTERCEPTION
# ============================================================
print_section "TEST 2: Attack WITH Interception Protection"

echo "[Setup] This test shows the effect of MR deregistration abuse"
echo "        when RDMA interception and quota limits are enabled."
echo ""
echo "Expected: Attacker is limited by quota,"
echo "          Victim's performance is protected."
echo ""

# Initialize tenant quota
echo "[Step 0] Setting up tenant quota..."
export LD_LIBRARY_PATH=/home/why/rdma_intercept_ldpreload/build:$LD_LIBRARY_PATH
/home/why/rdma_intercept_ldpreload/build/tenant_manager --create 20 --name "Attacker" --quota 20,100,1073741824 2>/dev/null || true
/home/why/rdma_intercept_ldpreload/build/tenant_manager --create 10 --name "Victim" --quota 20,100,1073741824 2>/dev/null || true
echo "[Step 0] Tenant quota configured"
echo ""

# Start victim in background
echo "[Step 1] Starting Victim workload..."
export LD_PRELOAD=$INTERCEPT_LIB
export RDMA_INTERCEPT_ENABLE=1
export RDMA_TENANT_ID=10
run_victim $RESULTS_DIR/victim_with_intercept.log $TEST_DURATION &
VICTIM_PID=$!
sleep 2

# Run attacker with interception
echo ""
echo "[Step 2] Starting Attacker (WITH interception)..."
run_attacker "true" $TEST_DURATION &
ATTACKER_PID=$!

# Wait for completion
echo ""
echo "[Step 3] Waiting for test completion (${TEST_DURATION}s)..."
wait $ATTACKER_PID
wait $VICTIM_PID

echo ""
echo -e "${GREEN}Test 2 Complete${NC}"
cp /tmp/victim_result.txt $RESULTS_DIR/victim_stats_with_intercept.txt

# ============================================================
# RESULTS COMPARISON
# ============================================================
print_section "RESULTS COMPARISON"

echo ""
echo "Victim Performance Metrics:"
echo "========================================"
echo ""

if [ -f $RESULTS_DIR/victim_stats_without_intercept.txt ]; then
    echo -e "${RED}WITHOUT Interception:${NC}"
    cat $RESULTS_DIR/victim_stats_without_intercept.txt
fi

echo ""

if [ -f $RESULTS_DIR/victim_stats_with_intercept.txt ]; then
    echo -e "${GREEN}WITH Interception:${NC}"
    cat $RESULTS_DIR/victim_stats_with_intercept.txt
fi

echo ""
echo "========================================"
echo ""
echo "Interpretation:"
echo "  - WITHOUT interception: Victim suffers from attacker's MR abuse"
echo "  - WITH interception: Attacker limited by quota, Victim protected"
echo ""
echo "Results saved to: $RESULTS_DIR"
echo ""
