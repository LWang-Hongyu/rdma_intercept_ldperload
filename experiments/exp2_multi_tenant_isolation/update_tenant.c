#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/shm/shared_memory_tenant.h"

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: %s <tenant_id> <qp_limit> <mr_limit> <memory_limit>\n", argv[0]);
        return 1;
    }
    
    uint32_t tenant_id = atoi(argv[1]);
    int qp_limit = atoi(argv[2]);
    int mr_limit = atoi(argv[3]);
    uint64_t memory_limit = atoll(argv[4]);
    
    if (tenant_shm_init() != 0) {
        fprintf(stderr, "Failed to init tenant shm\n");
        return 1;
    }
    
    tenant_info_t info;
    if (tenant_get_info(tenant_id, &info) != 0) {
        fprintf(stderr, "Tenant %u not found\n", tenant_id);
        return 1;
    }
    
    // Update quota
    tenant_quota_t new_quota;
    new_quota.max_qp_per_tenant = qp_limit;
    new_quota.max_mr_per_tenant = mr_limit;
    new_quota.max_memory_per_tenant = memory_limit;
    
    if (tenant_update_quota(tenant_id, &new_quota) == 0) {
        printf("Tenant %u updated successfully\n", tenant_id);
        printf("  New QP limit: %d\n", qp_limit);
        printf("  New MR limit: %d\n", mr_limit);
        printf("  New Memory limit: %lu\n", memory_limit);
    } else {
        fprintf(stderr, "Failed to update tenant %u\n", tenant_id);
        return 1;
    }
    
    return 0;
}
