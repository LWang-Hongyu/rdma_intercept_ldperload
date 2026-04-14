#include <stdio.h>
#include <stdlib.h>
#include "../../src/shm/shared_memory_tenant.h"

int main() {
    if (tenant_shm_init() != 0) {
        fprintf(stderr, "Failed to init tenant shm\n");
        return 1;
    }
    
    tenant_info_t info;
    if (tenant_get_info(50, &info) == 0) {
        printf("Tenant 50 Status:\n");
        printf("  Name: %s\n", info.tenant_name);
        printf("  Status: %d\n", info.status);
        printf("  QP Quota: %d\n", info.quota.max_qp_per_tenant);
        printf("  QP Usage: %d\n", info.usage.qp_count);
        printf("  MR Quota: %d\n", info.quota.max_mr_per_tenant);
        printf("  MR Usage: %d\n", info.usage.mr_count);
    } else {
        printf("Tenant 50 not found\n");
    }
    
    return 0;
}
