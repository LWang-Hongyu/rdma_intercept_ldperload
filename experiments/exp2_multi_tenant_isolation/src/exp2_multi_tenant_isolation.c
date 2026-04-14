/*
 * EXP-2: 多租户隔离验证测试程序
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <infiniband/verbs.h>
#include <stdint.h>

#define MAX_QP 200

typedef struct {
    int tenant_id;
    int quota;
    int extra_attempts;
    char *output_file;
} config_t;

static inline double get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  -t, --tenant ID      Tenant ID\n");
    printf("  -q, --quota N        QP quota\n");
    printf("  -e, --extra N        Extra attempts beyond quota (default: 10)\n");
    printf("  -o, --output FILE    Output file\n");
}

static int parse_args(int argc, char **argv, config_t *config) {
    config->tenant_id = -1;
    config->quota = 0;
    config->extra_attempts = 10;
    config->output_file = NULL;

    static struct option long_options[] = {
        {"tenant", required_argument, 0, 't'},
        {"quota", required_argument, 0, 'q'},
        {"extra", required_argument, 0, 'e'},
        {"output", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "t:q:e:o:h", long_options, NULL)) != -1) {
        switch (c) {
            case 't': config->tenant_id = atoi(optarg); break;
            case 'q': config->quota = atoi(optarg); break;
            case 'e': config->extra_attempts = atoi(optarg); break;
            case 'o': config->output_file = optarg; break;
            case 'h': print_usage(argv[0]); exit(0);
            default: return -1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    config_t config;
    if (parse_args(argc, argv, &config) != 0 || config.tenant_id < 0) {
        print_usage(argv[0]);
        return 1;
    }

    printf("[EXP-2] Tenant %d: Starting isolation test (quota=%d)\n", 
           config.tenant_id, config.quota);

    // 获取RDMA设备
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list || !dev_list[0]) {
        fprintf(stderr, "Error: No RDMA device\n");
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

    // 尝试创建 quota + extra 个QP
    struct ibv_qp *qps[MAX_QP];
    int created = 0;
    int denied = 0;
    double first_denial_time = 0;
    double latencies[MAX_QP];

    double start_time = get_time_us();
    int total_attempts = config.quota + config.extra_attempts;
    if (total_attempts > MAX_QP) total_attempts = MAX_QP;

    for (int i = 0; i < total_attempts; i++) {
        double op_start = get_time_us();
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        double op_end = get_time_us();

        latencies[i] = op_end - op_start;

        if (qp) {
            qps[created++] = qp;
        } else {
            if (denied == 0) {
                first_denial_time = op_end - start_time;
                printf("[Tenant %d] First denial at QP #%d (after %.1f ms)\n", 
                       config.tenant_id, i+1, first_denial_time / 1000.0);
            }
            denied++;
        }
    }

    double end_time = get_time_us();
    double total_time_ms = (end_time - start_time) / 1000.0;

    // 计算平均延迟（成功的）
    double total_latency = 0;
    for (int i = 0; i < created; i++) {
        total_latency += latencies[i];
    }
    double avg_latency = created > 0 ? total_latency / created : 0;

    printf("[Tenant %d] Result: Created=%d, Denied=%d, Time=%.2f ms\n",
           config.tenant_id, created, denied, total_time_ms);

    // 清理
    for (int i = 0; i < created; i++) {
        if (qps[i]) ibv_destroy_qp(qps[i]);
    }
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);

    // 保存结果
    FILE *fp = config.output_file ? fopen(config.output_file, "w") : stdout;
    if (fp) {
        fprintf(fp, "# EXP-2: Multi-Tenant Isolation Test\n");
        fprintf(fp, "TENANT_ID: %d\n", config.tenant_id);
        fprintf(fp, "QUOTA: %d\n", config.quota);
        fprintf(fp, "ATTEMPTS: %d\n", total_attempts);
        fprintf(fp, "CREATED: %d\n", created);
        fprintf(fp, "DENIED: %d\n", denied);
        fprintf(fp, "QUOTA_COMPLIANCE: %.1f%%\n", 
                created <= config.quota ? 100.0 : (double)config.quota/created*100);
        fprintf(fp, "FIRST_DENIAL_MS: %.2f\n", first_denial_time / 1000.0);
        fprintf(fp, "TOTAL_TIME_MS: %.2f\n", total_time_ms);
        fprintf(fp, "AVG_LATENCY_US: %.2f\n", avg_latency);
        
        fprintf(fp, "\n# Raw latency data (first 50)\n");
        fprintf(fp, "# QP_ID,LATENCY_US,SUCCESS\n");
        for (int i = 0; i < total_attempts && i < 50; i++) {
            fprintf(fp, "%d,%.2f,%d\n", i, latencies[i], i < created ? 1 : 0);
        }
        
        if (config.output_file) fclose(fp);
    }

    return 0;
}
