#!/bin/bash

# Reset tenant
./reset_tenant50

# Set quota to 5
cd /home/why/rdma_intercept_ldpreload
./experiments/exp2_multi_tenant_isolation/update_tenant 50 5 100 1073741824

cd /home/why/rdma_intercept_ldpreload/experiments/exp5_dynamic_policy

# Test with intercept
echo "Testing with intercept (quota=5, creating 12 QPs):"
export LD_PRELOAD="/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so"
export RDMA_TENANT_ID=50
export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1
export RDMA_INTERCEPT_ENABLE=1
./test_intercept15

# Check usage
echo ""
echo "Tenant status after test:"
./check_tenant50
