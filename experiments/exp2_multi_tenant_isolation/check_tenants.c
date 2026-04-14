/*
 * 检查租户状态工具
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../src/shm/shared_memory_tenant.h"

int main(int argc, char *argv[]) {
    printf("Checking tenant status...\n\n");
    
    // 初始化共享内存
    if (tenant_shm_init() != 0) {
        printf("Failed to initialize tenant shared memory\n");
        return 1;
    }
    
    printf("Checking specific tenants:\n");
    printf("%-10s %-20s %-10s %-10s %-10s\n", "ID", "Name", "QP", "MR", "Status");
    printf("--------------------------------------------------------\n");
    
    uint32_t tenant_ids[] = {1, 100, 200, 300, 400};
    int num_tenants = sizeof(tenant_ids) / sizeof(tenant_ids[0]);
    
    for (int i = 0; i < num_tenants; i++) {
        tenant_info_t info;
        if (tenant_get_info(tenant_ids[i], &info) == 0) {
            const char *status_str = "Unknown";
            switch (info.status) {
                case TENANT_STATUS_INACTIVE: status_str = "Inactive"; break;
                case TENANT_STATUS_ACTIVE: status_str = "Active"; break;
                case TENANT_STATUS_SUSPENDED: status_str = "Suspended"; break;
            }
            printf("%-10u %-20s %d/%-8d %d/%-8d %s\n",
                   info.tenant_id,
                   info.tenant_name,
                   info.usage.qp_count,
                   info.quota.max_qp_per_tenant,
                   info.usage.mr_count,
                   info.quota.max_mr_per_tenant,
                   status_str);
        } else {
            printf("%-10u %-20s %-10s %-10s %-10s\n",
                   tenant_ids[i], "NOT FOUND", "-", "-", "-");
        }
    }
    
    return 0;
}
