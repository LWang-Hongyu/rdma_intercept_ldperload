/*
 * 创建租户工具 - 直接操作共享内存
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../src/shm/shared_memory_tenant.h"

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: %s <tenant_id> <max_qp> <max_mr> <max_memory> [name]\n", argv[0]);
        printf("Example: %s 10 100 10 1073741824 TestTenant\n", argv[0]);
        return 1;
    }
    
    uint32_t tenant_id = atoi(argv[1]);
    uint32_t max_qp = atoi(argv[2]);
    uint32_t max_mr = atoi(argv[3]);
    uint64_t max_memory = strtoull(argv[4], NULL, 10);
    const char *name = (argc > 5) ? argv[5] : "TestTenant";
    
    printf("Creating tenant %u...\n", tenant_id);
    printf("  QP limit: %u\n", max_qp);
    printf("  MR limit: %u\n", max_mr);
    printf("  Memory limit: %lu\n", max_memory);
    
    // 初始化共享内存
    if (tenant_shm_init() != 0) {
        printf("Failed to initialize tenant shared memory\n");
        return 1;
    }
    
    // 创建租户
    tenant_quota_t quota;
    quota.max_qp_per_tenant = max_qp;
    quota.max_mr_per_tenant = max_mr;
    quota.max_memory_per_tenant = max_memory;
    
    int ret = tenant_create(tenant_id, name, &quota);
    if (ret != 0) {
        printf("Failed to create tenant: %d\n", ret);
        return 1;
    }
    
    printf("✓ Tenant %u created successfully\n", tenant_id);
    
    // 验证
    tenant_info_t info;
    if (tenant_get_info(tenant_id, &info) == 0) {
        printf("  Current QP: %d/%d\n", info.usage.qp_count, info.quota.max_qp_per_tenant);
        printf("  Current MR: %d/%d\n", info.usage.mr_count, info.quota.max_mr_per_tenant);
    }
    
    return 0;
}
