#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/shm/shared_memory_tenant.h"

int main() {
    if (tenant_shm_init() != 0) {
        fprintf(stderr, "Failed to init tenant shm\n");
        return 1;
    }
    
    uint32_t tenant_id = 50;
    
    // Get current info
    tenant_info_t info;
    if (tenant_get_info(tenant_id, &info) != 0) {
        fprintf(stderr, "Tenant %u not found\n", tenant_id);
        return 1;
    }
    
    printf("Before reset:\n");
    printf("  QP Usage: %u\n", info.usage.qp_count);
    
    // Reset usage to 0
    tenant_resource_usage_t usage = {0};
    if (tenant_update_resource_usage(tenant_id, &usage) != 0) {
        fprintf(stderr, "Failed to reset resource usage\n");
        return 1;
    }
    
    // Verify
    if (tenant_get_info(tenant_id, &info) != 0) {
        fprintf(stderr, "Failed to get info after reset\n");
        return 1;
    }
    
    printf("After reset:\n");
    printf("  QP Usage: %u\n", info.usage.qp_count);
    
    return 0;
}
