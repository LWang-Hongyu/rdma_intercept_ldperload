/*
 * 简单测试拦截是否工作
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <infiniband/verbs.h>

int main(int argc, char *argv[]) {
    int tenant_id = 1;
    if (argc > 1) {
        tenant_id = atoi(argv[1]);
    }
    
    printf("Test: Creating 5 QPs as tenant %d\n", tenant_id);
    
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list || !dev_list[0]) {
        fprintf(stderr, "No RDMA device\n");
        return 1;
    }
    
    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    struct ibv_cq *cq = ibv_create_cq(ctx, 16, NULL, NULL, 0);
    
    struct ibv_qp_init_attr qp_init_attr = {
        .qp_type = IBV_QPT_RC,
        .send_cq = cq,
        .recv_cq = cq,
        .cap = { .max_send_wr = 10, .max_recv_wr = 10, .max_send_sge = 1, .max_recv_sge = 1 }
    };
    
    int success = 0;
    int failed = 0;
    
    for (int i = 0; i < 5; i++) {
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        if (qp) {
            printf("  QP %d: CREATED\n", i+1);
            success++;
            ibv_destroy_qp(qp);
        } else {
            printf("  QP %d: FAILED (errno=%d)\n", i+1, errno);
            failed++;
        }
    }
    
    printf("\nResult: Success=%d, Failed=%d\n", success, failed);
    
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    return 0;
}
