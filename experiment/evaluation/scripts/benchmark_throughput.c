/*
 * benchmark_throughput.c
 * RDMA throughput benchmark for performance evaluation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <infiniband/verbs.h>

#define MAX_QPS 1024
#define TEST_DURATION_SEC 10
#define BATCH_SIZE 100

struct qp_context {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_qp *qp[MAX_QPS];
    int num_qps;
};

double get_time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

void* create_qp_worker(void* arg) {
    struct qp_context* ctx = (struct qp_context*)arg;
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_qp_attr qp_attr;

    // Initialize QP attributes
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq = ctx->cq;
    qp_init_attr.recv_cq = ctx->cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.cap.max_send_wr = 100;
    qp_init_attr.cap.max_recv_wr = 100;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    // Create QPs in batches
    for (int i = 0; i < ctx->num_qps; i += BATCH_SIZE) {
        int end_idx = (i + BATCH_SIZE < ctx->num_qps) ? i + BATCH_SIZE : ctx->num_qps;
        for (int j = i; j < end_idx; j++) {
            ctx->qp[j] = ibv_create_qp(ctx->pd, &qp_init_attr);
            if (!ctx->qp[j]) {
                fprintf(stderr, "Failed to create QP %d\n", j);
                return NULL;
            }
            
            // Modify QP to INIT state
            memset(&qp_attr, 0, sizeof(qp_attr));
            qp_attr.qp_state = IBV_QPS_INIT;
            qp_attr.port_num = 1;
            qp_attr.pkey_index = 0;
            qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
            
            if (ibv_modify_qp(ctx->qp[j], &qp_attr,
                             IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS)) {
                fprintf(stderr, "Failed to modify QP %d to INIT\n", j);
                return NULL;
            }
        }
        usleep(100); // Small delay to prevent overwhelming the system
    }

    return NULL;
}

int benchmark_throughput(int num_qps, int num_threads) {
    struct ibv_device **device_list;
    struct ibv_context *context;
    struct ibv_device_attr device_attr;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    pthread_t *threads;
    struct qp_context *contexts;
    struct timespec start_time, end_time;
    double duration, throughput;

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
    cq = ibv_create_cq(context, 1000, NULL, NULL, 0);
    if (!cq) {
        fprintf(stderr, "Failed to create CQ\n");
        ibv_dealloc_pd(pd);
        ibv_close_device(context);
        ibv_free_device_list(device_list);
        return -1;
    }

    // Prepare threads and contexts
    threads = malloc(num_threads * sizeof(pthread_t));
    contexts = malloc(num_threads * sizeof(struct qp_context));
    if (!threads || !contexts) {
        fprintf(stderr, "Failed to allocate thread structures\n");
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        ibv_close_device(context);
        ibv_free_device_list(device_list);
        free(threads);
        free(contexts);
        return -1;
    }

    // Initialize contexts
    for (int i = 0; i < num_threads; i++) {
        contexts[i].context = context;
        contexts[i].pd = pd;
        contexts[i].cq = cq;
        contexts[i].num_qps = num_qps / num_threads;
        if (i == num_threads - 1) {
            // Assign remaining QPs to last thread
            contexts[i].num_qps += num_qps % num_threads;
        }
    }

    printf("Starting throughput benchmark: %d QPs, %d threads\n", num_qps, num_threads);
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Create threads to create QPs in parallel
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, create_qp_worker, &contexts[i])) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            // Clean up and exit
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            goto cleanup;
        }
    }

    // Wait for all threads to complete
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    // Calculate results
    duration = get_time_diff(start_time, end_time);
    throughput = num_qps / duration;

    printf("Benchmark completed:\n");
    printf("  Duration: %.2f seconds\n", duration);
    printf("  Throughput: %.2f QPs/second\n", throughput);

cleanup:
    // Cleanup resources
    for (int i = 0; i < num_threads; i++) {
        for (int j = 0; j < contexts[i].num_qps; j++) {
            if (contexts[i].qp[j]) {
                ibv_destroy_qp(contexts[i].qp[j]);
            }
        }
    }
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(context);
    ibv_free_device_list(device_list);
    free(threads);
    free(contexts);

    return 0;
}

int main(int argc, char *argv[]) {
    int num_qps = 100;
    int num_threads = 4;

    if (argc >= 2) {
        num_qps = atoi(argv[1]);
    }
    if (argc >= 3) {
        num_threads = atoi(argv[2]);
    }

    printf("RDMA Throughput Benchmark\n");
    printf("=========================\n");

    return benchmark_throughput(num_qps, num_threads);
}