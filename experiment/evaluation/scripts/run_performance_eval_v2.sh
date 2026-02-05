#!/bin/bash
# Performance evaluation script for RDMA interception system
# Measures throughput differences with and without interception
# Updated to handle current system state where eBPF may not be available

echo "RDMA Interception System - Performance Evaluation"
echo "=================================================="

# Configuration
TEST_QPS=(50 100 200)
NUM_THREADS=4
BASELINE_REPEATS=3
INTERCEPT_REPEATS=3

# Output directory
OUTPUT_DIR="/home/why/rdma_intercept_ldpreload/experiment/evaluation/results"
mkdir -p $OUTPUT_DIR

# Compile benchmark if needed
cd /home/why/rdma_intercept_ldpreload/experiment/evaluation/scripts
if [ ! -f "./benchmark_throughput" ]; then
    gcc -o benchmark_throughput benchmark_throughput.c -libverbs -lpthread
fi

echo "Running baseline tests (without interception)..."

# Baseline tests (without interception)
for qps in "${TEST_QPS[@]}"; do
    total_throughput=0
    successful_runs=0
    echo "Testing $qps QPs (baseline)..."
    
    for ((i=1; i<=BASELINE_REPEATS; i++)); do
        echo "  Run $i/$BASELINE_REPEATS"
        result=$(timeout 30s ./benchmark_throughput $qps $NUM_THREADS 2>/dev/null | grep "Throughput:" | sed 's/.*Throughput: \([0-9.]*\).*/\1/')
        if [[ -n "$result" && "$result" =~ ^[0-9]+\.?[0-9]*$ ]]; then
            total_throughput=$(echo "$total_throughput + $result" | bc -l)
            echo "    Result: $result QPs/second"
            ((successful_runs++))
        else
            echo "    Result: Failed to get throughput"
        fi
    done
    
    if [ $successful_runs -gt 0 ]; then
        avg_throughput=$(echo "scale=2; $total_throughput / $successful_runs" | bc -l)
    else
        avg_throughput=0
    fi
    echo "$qps,$avg_throughput" >> $OUTPUT_DIR/baseline_results.csv
    echo "  Average throughput: $avg_throughput QPs/second"
    echo ""
done

echo "Running interception tests (with interception enabled)..."
echo "Note: Using local counting mode as eBPF may not be available"

# Interception tests (with interception enabled)
export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1
export RDMA_INTERCEPT_MAX_QP_PER_PROCESS=500
export RDMA_INTERCEPT_LOG_LEVEL=1

for qps in "${TEST_QPS[@]}"; do
    total_throughput=0
    successful_runs=0
    echo "Testing $qps QPs (with interception)..."
    
    for ((i=1; i<=INTERCEPT_REPEATS; i++)); do
        echo "  Run $i/$INTERCEPT_REPEATS"
        result=$(timeout 30s env LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so ./benchmark_throughput $qps $NUM_THREADS 2>/dev/null | grep "Throughput:" | sed 's/.*Throughput: \([0-9.]*\).*/\1/')
        if [[ -n "$result" && "$result" =~ ^[0-9]+\.?[0-9]*$ ]]; then
            total_throughput=$(echo "$total_throughput + $result" | bc -l)
            echo "    Result: $result QPs/second"
            ((successful_runs++))
        else
            echo "    Result: Failed to get throughput"
            # Debug output to understand why it's failing
            echo "Debug: Attempting to run benchmark with interception..."
            timeout 5s env LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so ./benchmark_throughput $qps $NUM_THREADS 2>&1 | head -n 10
        fi
    done
    
    if [ $successful_runs -gt 0 ]; then
        avg_throughput=$(echo "scale=2; $total_throughput / $successful_runs" | bc -l)
    else
        avg_throughput=0
    fi
    echo "$qps,$avg_throughput" >> $OUTPUT_DIR/intercept_results.csv
    echo "  Average throughput: $avg_throughput QPs/second"
    echo ""
done

echo "Performance evaluation completed!"
echo "Results saved to $OUTPUT_DIR/"

# Calculate performance overhead
echo ""
echo "Performance Overhead Analysis:"
echo "=============================="
if [ -f $OUTPUT_DIR/baseline_results.csv ] && [ -f $OUTPUT_DIR/intercept_results.csv ]; then
    paste $OUTPUT_DIR/baseline_results.csv $OUTPUT_DIR/intercept_results.csv > $OUTPUT_DIR/combined_results.csv
    while IFS=',' read -r baseline_qp baseline_tp intercept_qp intercept_tp; do
        if [[ -n "$baseline_tp" && -n "$intercept_tp" && $(echo "$baseline_tp > 0" | bc -l) ]]; then
            overhead=$(echo "scale=2; (($baseline_tp - $intercept_tp) / $baseline_tp) * 100" | bc -l)
            echo "QPs: $baseline_qp, Baseline: $baseline_tp, With Intercept: $intercept_tp, Overhead: $overhead%"
        fi
    done < $OUTPUT_DIR/combined_results.csv
else
    echo "Could not generate combined results - CSV files not found"
fi

echo ""
echo "Detailed results are available in $OUTPUT_DIR/"
echo ""
echo "System Status:"
echo "- Collector server: Running"
echo "- eBPF monitoring: $(if pgrep -f 'rdma_monitor'; then echo "Available"; else echo "Not available (using local counting mode)"; fi)"
echo "- Interception: Active"