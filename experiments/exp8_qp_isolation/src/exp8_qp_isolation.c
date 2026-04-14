/*
 * EXP-8: QP资源隔离验证测试程序
 * 
 * 测试目的: 验证一个租户的大量QP创建不会影响另一个租户的QP创建能力
 * 
 * 使用方法:
 *   ./exp8_qp_isolation --tenant 10 --create 10 --output result.txt
 *   ./exp8_qp_isolation -t 10 -n 10 -o result.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <stdint.h>

#define MAX_QP 256
#define DEFAULT_TENANT_ID 10
#define DEFAULT_NUM_QP 10

typedef struct {
    uint32_t tenant_id;
    int num_qp_to_create;
    char *output_file;
    int verbose;
} config_t;

typedef struct {
    double start_time;
    double end_time;
    int success;
    int error_code;
} qp_create_record_t;

static inline double get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -t, --tenant ID      Tenant ID (default: %d)\n", DEFAULT_TENANT_ID);
    printf("  -n, --num-qp N       Number of QPs to create (default: %d)\n", DEFAULT_NUM_QP);
    printf("  -o, --output FILE    Output file for results\n");
    printf("  -v, --verbose        Verbose output\n");
    printf("  -h, --help           Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -t 10 -n 10                    # Tenant 10 creates 10 QPs\n", prog);
    printf("  %s -t 20 -n 100 -o attacker.txt   # Tenant 20 attempts 100 QPs\n", prog);
}

static int parse_args(int argc, char **argv, config_t *config) {
    config->tenant_id = DEFAULT_TENANT_ID;
    config->num_qp_to_create = DEFAULT_NUM_QP;
    config->output_file = NULL;
    config->verbose = 0;

    static struct option long_options[] = {
        {"tenant", required_argument, 0, 't'},
        {"num-qp", required_argument, 0, 'n'},
        {"output", required_argument, 0, 'o'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "t:n:o:vh", long_options, NULL)) != -1) {
        switch (c) {
            case 't':
                config->tenant_id = atoi(optarg);
                break;
            case 'n':
                config->num_qp_to_create = atoi(optarg);
                break;
            case 'o':
                config->output_file = optarg;
                break;
            case 'v':
                config->verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    if (config->num_qp_to_create > MAX_QP) {
        fprintf(stderr, "Error: num-qp exceeds maximum (%d)\n", MAX_QP);
        return -1;
    }

    return 0;
}

static int run_qp_isolation_test(config_t *config, qp_create_record_t *records, 
                                  struct ibv_qp **qp_array) {
    struct ibv_device **dev_list = NULL;
    struct ibv_context *ctx = NULL;
    struct ibv_pd *pd = NULL;
    struct ibv_cq *cq = NULL;
    int success_count = 0;
    int fail_count = 0;

    // Get RDMA device
    dev_list = ibv_get_device_list(NULL);
    if (!dev_list || !dev_list[0]) {
        fprintf(stderr, "[Tenant %u] Error: No RDMA device found\n", config->tenant_id);
        return -1;
    }

    if (config->verbose) {
        printf("[Tenant %u] Using device: %s\n", config->tenant_id, 
               ibv_get_device_name(dev_list[0]));
    }

    // Open device
    ctx = ibv_open_device(dev_list[0]);
    if (!ctx) {
        fprintf(stderr, "[Tenant %u] Error: Failed to open device\n", config->tenant_id);
        goto cleanup;
    }

    // Allocate PD
    pd = ibv_alloc_pd(ctx);
    if (!pd) {
        fprintf(stderr, "[Tenant %u] Error: Failed to allocate PD\n", config->tenant_id);
        goto cleanup;
    }

    // Create CQ
    cq = ibv_create_cq(ctx, 16, NULL, NULL, 0);
    if (!cq) {
        fprintf(stderr, "[Tenant %u] Error: Failed to create CQ\n", config->tenant_id);
        goto cleanup;
    }

    // QP init attributes
    struct ibv_qp_init_attr qp_init_attr = {
        .qp_type = IBV_QPT_RC,
        .send_cq = cq,
        .recv_cq = cq,
        .cap = {
            .max_send_wr = 10,
            .max_recv_wr = 10,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };

    if (config->verbose) {
        printf("[Tenant %u] Starting to create %d QPs...\n", 
               config->tenant_id, config->num_qp_to_create);
    }

    // Create QPs and measure latency
    for (int i = 0; i < config->num_qp_to_create; i++) {
        records[i].start_time = get_time_us();
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        records[i].end_time = get_time_us();
        
        if (qp) {
            records[i].success = 1;
            records[i].error_code = 0;
            qp_array[i] = qp;
            success_count++;
            
            if (config->verbose && (i < 5 || i == config->num_qp_to_create - 1)) {
                printf("[Tenant %u] QP %d: Created OK (%.2f us)\n", 
                       config->tenant_id, i, 
                       records[i].end_time - records[i].start_time);
            }
        } else {
            records[i].success = 0;
            records[i].error_code = errno;
            qp_array[i] = NULL;
            fail_count++;
            
            if (config->verbose) {
                printf("[Tenant %u] QP %d: FAILED (errno=%d, %.2f us)\n", 
                       config->tenant_id, i, errno,
                       records[i].end_time - records[i].start_time);
            }
        }
    }

    if (config->verbose) {
        printf("[Tenant %u] Test complete: Success=%d, Failed=%d\n", 
               config->tenant_id, success_count, fail_count);
    }

    // Cleanup QPs
    for (int i = 0; i < config->num_qp_to_create; i++) {
        if (qp_array[i]) {
            ibv_destroy_qp(qp_array[i]);
        }
    }

cleanup:
    if (cq) ibv_destroy_cq(cq);
    if (pd) ibv_dealloc_pd(pd);
    if (ctx) ibv_close_device(ctx);
    if (dev_list) ibv_free_device_list(dev_list);

    return success_count;
}

static void write_results(config_t *config, qp_create_record_t *records, 
                          int success_count, int total_count) {
    FILE *fp = stdout;
    
    if (config->output_file) {
        fp = fopen(config->output_file, "w");
        if (!fp) {
            fprintf(stderr, "Warning: Cannot open output file, using stdout\n");
            fp = stdout;
        }
    }

    // Calculate statistics
    double total_latency = 0;
    double min_latency = 1e9;
    double max_latency = 0;
    int valid_count = 0;

    for (int i = 0; i < total_count; i++) {
        if (records[i].success) {
            double lat = records[i].end_time - records[i].start_time;
            total_latency += lat;
            if (lat < min_latency) min_latency = lat;
            if (lat > max_latency) max_latency = lat;
            valid_count++;
        }
    }

    double avg_latency = valid_count > 0 ? total_latency / valid_count : 0;

    // Write results
    fprintf(fp, "# EXP-8: QP Resource Isolation Test Results\n");
    fprintf(fp, "# Tenant ID: %u\n", config->tenant_id);
    fprintf(fp, "# Requested QPs: %d\n", total_count);
    fprintf(fp, "# Successful: %d\n", success_count);
    fprintf(fp, "# Failed: %d\n", total_count - success_count);
    fprintf(fp, "# Success Rate: %.1f%%\n", 
            total_count > 0 ? (100.0 * success_count / total_count) : 0);
    fprintf(fp, "#\n");
    fprintf(fp, "# Latency Statistics (successful creations only):\n");
    fprintf(fp, "#   Average: %.2f us\n", avg_latency);
    fprintf(fp, "#   Min: %.2f us\n", valid_count > 0 ? min_latency : 0);
    fprintf(fp, "#   Max: %.2f us\n", valid_count > 0 ? max_latency : 0);
    fprintf(fp, "#\n");
    fprintf(fp, "# Per-QP Details:\n");
    fprintf(fp, "# QP_ID,SUCCESS,LATENCY_US,ERROR_CODE\n");

    for (int i = 0; i < total_count; i++) {
        double lat = records[i].end_time - records[i].start_time;
        fprintf(fp, "%d,%d,%.2f,%d\n", 
                i, records[i].success, lat, records[i].error_code);
    }

    if (fp != stdout) {
        fclose(fp);
        printf("[Tenant %u] Results written to: %s\n", 
               config->tenant_id, config->output_file);
    }

    // Print summary to stdout
    printf("\n[Tenant %u] Summary:\n", config->tenant_id);
    printf("  Requested: %d QPs\n", total_count);
    printf("  Success:   %d QPs (%.1f%%)\n", 
           success_count, total_count > 0 ? (100.0 * success_count / total_count) : 0);
    printf("  Failed:    %d QPs (%.1f%%)\n", 
           total_count - success_count, 
           total_count > 0 ? (100.0 * (total_count - success_count) / total_count) : 0);
    printf("  Avg Latency: %.2f us\n", avg_latency);
}

int main(int argc, char **argv) {
    config_t config;
    qp_create_record_t records[MAX_QP];
    struct ibv_qp *qp_array[MAX_QP];

    printf("========================================\n");
    printf("EXP-8: QP Resource Isolation Test\n");
    printf("========================================\n\n");

    if (parse_args(argc, argv, &config) != 0) {
        return 1;
    }

    printf("Configuration:\n");
    printf("  Tenant ID: %u\n", config.tenant_id);
    printf("  Num QPs to create: %d\n", config.num_qp_to_create);
    printf("  Output file: %s\n", config.output_file ? config.output_file : "stdout");
    printf("  Verbose: %s\n", config.verbose ? "yes" : "no");
    printf("\n");

    memset(records, 0, sizeof(records));
    memset(qp_array, 0, sizeof(qp_array));

    int success_count = run_qp_isolation_test(&config, records, qp_array);
    if (success_count < 0) {
        fprintf(stderr, "Test failed\n");
        return 1;
    }

    write_results(&config, records, success_count, config.num_qp_to_create);

    return 0;
}
