#!/bin/bash
# Latency test script to measure RDMA operation delays with and without interception

echo "RDMA Latency Measurement Test"
echo "============================="

# Compile the latency test program
cd /home/why/rdma_intercept_ldpreload/experiment/evaluation/scripts
gcc -o latency_test latency_test.c -libverbs

echo "Running baseline latency test (without interception)..."

# Baseline test (without interception)
baseline_result=$(timeout 30s ./latency_test 2>/dev/null | grep "Average time per QP operation:" | awk '{print $(NF-1)}' | sed 's/[(,)]//g')
if [[ -n "$baseline_result" ]]; then
    echo "Baseline average QP operation time: $baseline_result ms"
else
    echo "Baseline test failed to produce results"
    baseline_result=0
fi

echo ""
echo "Running interception latency test (with interception)..."

# Interception test (with interception enabled)
export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_MAX_QP=500
export RDMA_INTERCEPT_LOG_LEVEL=1

intercept_result=$(timeout 30s env LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so ./latency_test 2>/dev/null | grep "Average time per QP operation:" | awk '{print $(NF-1)}' | sed 's/[(,)]//g')
if [[ -n "$intercept_result" ]]; then
    echo "With interception average QP operation time: $intercept_result ms"
else
    echo "Interception test failed to produce results"
    intercept_result=0
fi

echo ""
echo "Latency Analysis:"
if [[ $(echo "$baseline_result > 0" | bc -l) ]] && [[ $(echo "$intercept_result > 0" | bc -l) ]]; then
    latency_increase=$(echo "scale=3; $intercept_result - $baseline_result" | bc -l)
    latency_overhead=$(echo "scale=2; ($latency_increase / $baseline_result) * 100" | bc -l)
    
    echo "  Baseline: $baseline_result ms"
    echo "  With interception: $intercept_result ms"
    echo "  Absolute increase: $latency_increase ms"
    echo "  Relative overhead: $latency_overhead%"
else
    echo "  Could not calculate latency overhead due to test failures"
fi

echo ""
echo "Note: These measurements represent the average time to create and initialize a QP."
echo "The overhead represents the additional delay introduced by the interception system."