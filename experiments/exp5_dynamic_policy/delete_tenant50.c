#include <stdio.h>
#include <stdlib.h>
#include "../../src/shm/shared_memory_tenant.h"

int main() {
    if (tenant_shm_init() != 0) {
        fprintf(stderr, "Failed to init tenant shm\n");
        return 1;
    }
    
    if (tenant_delete(50) == 0) {
        printf("Tenant 50 deleted successfully\n");
    } else {
        printf("Failed to delete tenant 50 (may not exist)\n");
    }
    
    return 0;
}
