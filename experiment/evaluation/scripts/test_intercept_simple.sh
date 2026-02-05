#!/bin/bash
# Simple test to verify intercept functionality
echo "Testing interception performance..."

export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_MAX_QP=500
export RDMA_INTERCEPT_LOG_LEVEL=1

result=$(timeout 30s env LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so ./benchmark_throughput 10 2 2>/dev/null | grep "Throughput:" | sed 's/.*Throughput: \([0-9.]*\).*/\1/')

if [[ -n "$result" && "$result" =~ ^[0-9]+\.?[0-9]*$ ]]; then
    echo "Success: Got throughput result = $result QPs/second"
else
    echo "Failed: Could not extract throughput result"
    echo "Raw output test:"
    timeout 10s env LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so ./benchmark_throughput 10 2 2>&1
fi