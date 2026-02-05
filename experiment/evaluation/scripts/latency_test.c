/*
 * latency_test.c
 * RDMA latency test for measuring operation delays
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <infiniband/verbs.h>

#define MAX_QP_COUNT 100

double get_time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

int main() {
    struct ibv_device **device_list;
    struct ibv_context *context;
    struct ibv_device_attr device_attr;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_qp *qp[MAX_QP_COUNT];
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_qp_attr qp_attr;
    struct timespec start_time, end_time;
    double total_time, avg_time;
    int num_qps = 20; // Test with 20 QPs
    int i;

    printf("RDMA Latency Test\n");
    printf("=================\n");

    // Get device list
    device_list = ibv_get_device_list(NULL);
    if (!device_list) {
        fprintf(stderr, "No RDMA devices found\n");
        return -1;
    }

    // Open device
    context = ibv_open_device(device_list[0]);
    if (!context) {
        fprintf(stderr, "Failed to open device\n");
        ibv_free_device_list(device_list);
        return -1;
    }

    // Query device attributes
    if (ibv_query_device(context, &device_attr)) {
        fprintf(stderr, "Failed to query device attributes\n");
        ibv_close_device(context);
        ibv_free_device_list(device_list);
        return -1;
    }

    // Allocate protection domain
    pd = ibv_alloc_pd(context);
    if (!pd) {
        fprintf(stderr, "Failed to allocate PD\n");
        ibv_close_device(context);
        ibv_free_device_list(device_list);
        return -1;
    }

    // Create completion queue
    cq = ibv_create_cq(context, num_qps, NULL, NULL, 0);
    if (!cq) {
        fprintf(stderr, "Failed to create CQ\n");
        ibv_dealloc_pd(pd);
        ibv_close_device(context);
        ibv_free_device_list(device_list);
        return -1;
    }

    // Initialize QP attributes
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.cap.max_send_wr = 10;
    qp_init_attr.cap.max_recv_wr = 10;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    printf("Measuring QP creation latency for %d QPs...\n", num_qps);

    // Measure total time for creating all QPs
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    for (i = 0; i < num_qps; i++) {
        qp[i] = ibv_create_qp(pd, &qp_init_attr);
        if (!qp[i]) {
            fprintf(stderr, "Failed to create QP %d\n", i);
            // Clean up and exit
            for (int j = 0; j < i; j++) {
                ibv_destroy_qp(qp[j]);
            }
            ibv_destroy_cq(cq);
            ibv_dealloc_pd(pd);
            ibv_close_device(context);
            ibv_free_device_list(device_list);
            return -1;
        }

        // Modify QP to INIT state
        memset(&qp_attr, 0, sizeof(qp_attr));
        qp_attr.qp_state = IBV_QPS_INIT;
        qp_attr.port_num = 1;
        qp_attr.pkey_index = 0;
        qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

        if (ibv_modify_qp(qp[i], &qp_attr,
                         IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS)) {
            fprintf(stderr, "Failed to modify QP %d to INIT\n", i);
            // Clean up and exit
            for (int j = 0; j <= i; j++) {
                ibv_destroy_qp(qp[j]);
            }
            ibv_destroy_cq(cq);
            ibv_dealloc_pd(pd);
            ibv_close_device(context);
            ibv_free_device_list(device_list);
            return -1;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    // Calculate average time per QP
    total_time = get_time_diff(start_time, end_time);
    avg_time = total_time / num_qps;

    printf("\nTest Results:\n");
    printf("  Total time for %d QP operations: %.6f seconds\n", num_qps, total_time);
    printf("  Average time per QP operation: %.6f seconds (%.3f ms)\n", avg_time, avg_time * 1000);

    // Clean up
    for (i = 0; i < num_qps; i++) {
        ibv_destroy_qp(qp[i]);
    }
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(context);
    ibv_free_device_list(device_list);

    printf("\nLatency test completed.\n");
    return 0;
}