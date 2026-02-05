#!/bin/bash
# Performance evaluation script for RDMA interception system
# Measures throughput differences with and without interception

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
gcc -o benchmark_throughput benchmark_throughput.c -libverbs -lpthread

echo "Running baseline tests (without interception)..."

# Baseline tests (without interception)
for qps in "${TEST_QPS[@]}"; do
    total_throughput=0
    echo "Testing $qps QPs (baseline)..."
    
    for ((i=1; i<=BASELINE_REPEATS; i++)); do
        echo "  Run $i/$BASELINE_REPEATS"
        result=$(timeout 30s ./benchmark_throughput $qps $NUM_THREADS 2>/dev/null | grep "Throughput:" | sed 's/.*Throughput: \([0-9.]*\).*/\1/')
        if [[ -n "$result" && "$result" != *"QPs/second"* && "$result" =~ ^[0-9]+\.?[0-9]*$ ]]; then
            total_throughput=$(echo "$total_throughput + $result" | bc -l)
            echo "    Result: $result QPs/second"
        else
            echo "    Result: Failed to get throughput"
            result=0
        fi
    done
    
    avg_throughput=$(echo "$total_throughput / $BASELINE_REPEATS" | bc -l)
    echo "$qps,$avg_throughput" >> $OUTPUT_DIR/baseline_results.csv
    echo "  Average throughput: $avg_throughput QPs/second"
    echo ""
done

echo "Running interception tests (with interception enabled)..."

# Interception tests (with interception enabled)
export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_MAX_QP=500
export RDMA_INTERCEPT_LOG_LEVEL=1

for qps in "${TEST_QPS[@]}"; do
    total_throughput=0
    echo "Testing $qps QPs (with interception)..."
    
    for ((i=1; i<=INTERCEPT_REPEATS; i++)); do
        echo "  Run $i/$INTERCEPT_REPEATS"
        result=$(timeout 30s LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so ./benchmark_throughput $qps $NUM_THREADS 2>/dev/null | grep "Throughput:" | sed 's/.*Throughput: \([0-9.]*\).*/\1/')
        if [[ -n "$result" && "$result" != *"QPs/second"* && "$result" =~ ^[0-9]+\.?[0-9]*$ ]]; then
            total_throughput=$(echo "$total_throughput + $result" | bc -l)
            echo "    Result: $result QPs/second"
        else
            echo "    Result: Failed to get throughput"
            result=0
        fi
    done
    
    avg_throughput=$(echo "$total_throughput / $INTERCEPT_REPEATS" | bc -l)
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
paste $OUTPUT_DIR/baseline_results.csv $OUTPUT_DIR/intercept_results.csv > $OUTPUT_DIR/combined_results.csv
while IFS=',' read -r baseline_qp baseline_tp intercept_qp intercept_tp; do
    if [[ -n "$baseline_tp" && -n "$intercept_tp" && $(echo "$baseline_tp > 0" | bc -l) ]]; then
        overhead=$(echo "scale=2; (($baseline_tp - $intercept_tp) / $baseline_tp) * 100" | bc -l)
        echo "QPs: $baseline_qp, Baseline: $baseline_tp, With Intercept: $intercept_tp, Overhead: $overhead%"
    fi
done < $OUTPUT_DIR/combined_results.csv

echo ""
echo "Detailed results are available in $OUTPUT_DIR/"