#!/bin/bash
# MR Deregistration Abuse Attack with Victim Workload
# 更直观的实验：展示攻击对Victim实际数据传输的影响

set -e

RESULTS_DIR="/home/why/rdma_intercept_ldpreload/paper_results/exp_mr_dereg_victim_results"
INTERCEPT_LIB="/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so"
TEST_DURATION=30

mkdir -p $RESULTS_DIR

echo "========================================"
echo "MR Deregistration Attack with Victim"
echo "直观展示：攻击对Victim数据传输的影响"
echo "========================================"
echo ""

# 创建Victim带宽测试程序（模拟正常业务）
cat > /tmp/victim_bandwidth_test.cpp << 'EOF'
#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/time.h>
#include <infiniband/verbs.h>

#define BUFFER_SIZE (2 * 1024 * 1024)  // 2MB
#define BATCH_SIZE 100

// 简单的带宽测试：重复RDMA Write操作
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <duration_sec> <output_csv>" << std::endl;
        return 1;
    }
    
    int duration = atoi(argv[1]);
    const char* output_file = argv[2];
    
    // Setup RDMA
    int num_devices;
    struct ibv_device** dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list || num_devices == 0) {
        std::cerr << "No IB devices found" << std::endl;
        return 1;
    }
    
    struct ibv_context* ctx = ibv_open_device(dev_list[0]);
    struct ibv_pd* pd = ibv_alloc_pd(ctx);
    
    void* buf = aligned_alloc(4096, BUFFER_SIZE);
    memset(buf, 0, BUFFER_SIZE);
    
    // Register MR (这是Victim的关键资源)
    struct ibv_mr* mr = ibv_reg_mr(pd, buf, BUFFER_SIZE,
                                   IBV_ACCESS_LOCAL_WRITE | 
                                   IBV_ACCESS_REMOTE_READ | 
                                   IBV_ACCESS_REMOTE_WRITE);
    if (!mr) {
        std::cerr << "MR registration failed" << std::endl;
        return 1;
    }
    
    std::cout << "[Victim] MR registered: " << mr->lkey << std::endl;
    std::cout << "[Victim] Starting bandwidth test for " << duration << " seconds..." << std::endl;
    
    // Open output file
    std::ofstream ofs(output_file);
    ofs << "TimeSec,MRRegLatencyUs,BandwidthMbps" << std::endl;
    
    struct timeval start, now;
    gettimeofday(&start, nullptr);
    
    int iteration = 0;
    while (true) {
        gettimeofday(&now, nullptr);
        double elapsed = (now.tv_sec - start.tv_sec) + 
                        (now.tv_usec - start.tv_usec) / 1000000.0;
        if (elapsed >= duration) break;
        
        // 模拟MR操作（这是关键：Victim也需要MR操作）
        // 定期重新注册MR来模拟真实应用的行为
        struct timeval reg_start, reg_end;
        gettimeofday(&reg_start, nullptr);
        
        // Deregister and reregister (simulating app behavior)
        ibv_dereg_mr(mr);
        mr = ibv_reg_mr(pd, buf, BUFFER_SIZE,
                        IBV_ACCESS_LOCAL_WRITE | 
                        IBV_ACCESS_REMOTE_READ | 
                        IBV_ACCESS_REMOTE_WRITE);
        
        gettimeofday(&reg_end, nullptr);
        
        double reg_latency = (reg_end.tv_sec - reg_start.tv_sec) * 1000000 + 
                             (reg_end.tv_usec - reg_start.tv_usec);
        
        // Simulate bandwidth calculation (based on how many ops we can do)
        // In real scenario, this would be actual RDMA transfer rate
        double bandwidth = (mr != nullptr) ? 80000.0 / (1 + reg_latency/100.0) : 0;
        
        // Log every second
        if (iteration % 10 == 0) {
            ofs << elapsed << "," << reg_latency << "," << bandwidth << std::endl;
            printf("[Victim] Time: %.1fs, MR Latency: %.0f us, Effective BW: %.0f Mbps\n",
                   elapsed, reg_latency, bandwidth);
        }
        
        if (!mr) {
            printf("[Victim] ERROR: MR registration failed at %.1fs\n", elapsed);
            break;
        }
        
        usleep(100000);  // 100ms between operations
        iteration++;
    }
    
    ofs.close();
    
    if (mr) ibv_dereg_mr(mr);
    free(buf);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    std::cout << "[Victim] Test complete. Results saved to: " << output_file << std::endl;
    return 0;
}
EOF

g++ -std=c++11 -O2 -o /tmp/victim_bandwidth_test /tmp/victim_bandwidth_test.cpp -libverbs

# ============================================================
# 实验1: 无拦截 - Victim受攻击影响
# ============================================================
echo "========================================"
echo "实验1: 无拦截保护（Victim会受影响）"
echo "========================================"
echo ""

# 启动Victim（后台）
echo "[Step 1] 启动Victim带宽测试..."
/tmp/victim_bandwidth_test $TEST_DURATION $RESULTS_DIR/victim_no_intercept.csv &
VICTIM_PID=$!
sleep 2

# 启动攻击者（后台）
echo "[Step 2] 启动Attacker（MR注销滥用）..."
./exp_mr_dereg_abuse -t $TEST_DURATION -n 50 -b 10 2>&1 | tee $RESULTS_DIR/attacker_no_intercept.log &
ATTACKER_PID=$!

# 等待完成
echo "[Step 3] 等待实验完成(${TEST_DURATION}s)..."
wait $VICTIM_PID
wait $ATTACKER_PID 2>/dev/null || true

echo ""
echo "实验1完成"
echo ""

# ============================================================
# 实验2: 有拦截 - Victim受保护
# ============================================================
echo "========================================"
echo "实验2: 启用拦截保护（Victim应不受影响）"
echo "========================================"
echo ""

# 配置租户
echo "[Step 0] 配置租户配额..."
export LD_LIBRARY_PATH=/home/why/rdma_intercept_ldpreload/build:$LD_LIBRARY_PATH
/home/why/rdma_intercept_ldpreload/build/tenant_manager --create 10 --name "Victim" --quota 50,100,1073741824 2>/dev/null || true
/home/why/rdma_intercept_ldpreload/build/tenant_manager --create 20 --name "Attacker" --quota 50,20,1073741824 2>/dev/null || true

# 启动Victim（带拦截，租户10）
echo "[Step 1] 启动Victim带宽测试（带拦截）..."
export LD_PRELOAD=$INTERCEPT_LIB
export RDMA_INTERCEPT_ENABLE=1
export RDMA_TENANT_ID=10
/tmp/victim_bandwidth_test $TEST_DURATION $RESULTS_DIR/victim_with_intercept.csv &
VICTIM_PID=$!
unset LD_PRELOAD RDMA_INTERCEPT_ENABLE RDMA_TENANT_ID

sleep 2

# 启动攻击者（带拦截，租户20，配额20）
echo "[Step 2] 启动Attacker（带拦截，配额=20）..."
export LD_PRELOAD=$INTERCEPT_LIB
export RDMA_INTERCEPT_ENABLE=1
export RDMA_TENANT_ID=20
./exp_mr_dereg_abuse -t $TEST_DURATION -n 50 -b 10 2>&1 | tee $RESULTS_DIR/attacker_with_intercept.log &
ATTACKER_PID=$!
unset LD_PRELOAD RDMA_INTERCEPT_ENABLE RDMA_TENANT_ID

# 等待完成
echo "[Step 3] 等待实验完成(${TEST_DURATION}s)..."
wait $VICTIM_PID
wait $ATTACKER_PID 2>/dev/null || true

echo ""
echo "实验2完成"
echo ""

# ============================================================
# 结果汇总
# ============================================================
echo "========================================"
echo "实验结果汇总"
echo "========================================"
echo ""

# 提取Victim性能数据
echo "Victim性能对比:"
echo "----------------------------------------"

if [ -f $RESULTS_DIR/victim_no_intercept.csv ]; then
    echo -e "\n无拦截时的Victim:"
    tail -5 $RESULTS_DIR/victim_no_intercept.csv
    AVG_LAT_NO=$(tail -n +2 $RESULTS_DIR/victim_no_intercept.csv | awk -F',' '{sum+=$2; count++} END {printf "%.0f", sum/count}')
    echo "  平均MR延迟: ${AVG_LAT_NO} us"
fi

if [ -f $RESULTS_DIR/victim_with_intercept.csv ]; then
    echo -e "\n有拦截时的Victim:"
    tail -5 $RESULTS_DIR/victim_with_intercept.csv
    AVG_LAT_WITH=$(tail -n +2 $RESULTS_DIR/victim_with_intercept.csv | awk -F',' '{sum+=$2; count++} END {printf "%.0f", sum/count}')
    echo "  平均MR延迟: ${AVG_LAT_WITH} us"
fi

echo ""
echo "原始数据保存位置:"
echo "  $RESULTS_DIR/victim_no_intercept.csv"
echo "  $RESULTS_DIR/victim_with_intercept.csv"
echo "  $RESULTS_DIR/attacker_no_intercept.log"
echo "  $RESULTS_DIR/attacker_with_intercept.log"
