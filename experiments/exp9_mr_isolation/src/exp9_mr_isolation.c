/*
 * EXP-9: MR资源隔离验证测试程序
 * 
 * 测试目的: 验证一个租户的大量MR注册不会影响另一个租户的MR注册能力
 * 
 * 使用方法:
 *   ./exp9_mr_isolation --tenant 10 --register 10 --mr-size 4096 --output result.txt
 *   ./exp9_mr_isolation -t 10 -n 10 -s 4096 -o result.txt
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

#define MAX_MR 256
#define DEFAULT_TENANT_ID 10
#define DEFAULT_NUM_MR 10
#define DEFAULT_MR_SIZE 4096

typedef struct {
    uint32_t tenant_id;
    int num_mr_to_register;
    size_t mr_size;
    char *output_file;
    int verbose;
} config_t;

typedef struct {
    double start_time;
    double end_time;
    int success;
    int error_code;
} mr_reg_record_t;

static inline double get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -t, --tenant ID      Tenant ID (default: %d)\n", DEFAULT_TENANT_ID);
    printf("  -n, --num-mr N       Number of MRs to register (default: %d)\n", DEFAULT_NUM_MR);
    printf("  -s, --mr-size SIZE   MR size in bytes (default: %d)\n", DEFAULT_MR_SIZE);
    printf("  -o, --output FILE    Output file for results\n");
    printf("  -v, --verbose        Verbose output\n");
    printf("  -h, --help           Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -t 10 -n 10                    # Tenant 10 registers 10 MRs\n", prog);
    printf("  %s -t 20 -n 100 -s 4096           # Tenant 20 attempts 100 MRs\n", prog);
}

static int parse_args(int argc, char **argv, config_t *config) {
    config->tenant_id = DEFAULT_TENANT_ID;
    config->num_mr_to_register = DEFAULT_NUM_MR;
    config->mr_size = DEFAULT_MR_SIZE;
    config->output_file = NULL;
    config->verbose = 0;

    static struct option long_options[] = {
        {"tenant", required_argument, 0, 't'},
        {"num-mr", required_argument, 0, 'n'},
        {"mr-size", required_argument, 0, 's'},
        {"output", required_argument, 0, 'o'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "t:n:s:o:vh", long_options, NULL)) != -1) {
        switch (c) {
            case 't':
                config->tenant_id = atoi(optarg);
                break;
            case 'n':
                config->num_mr_to_register = atoi(optarg);
                break;
            case 's':
                config->mr_size = atol(optarg);
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

    if (config->num_mr_to_register > MAX_MR) {
        fprintf(stderr, "Error: num-mr exceeds maximum (%d)\n", MAX_MR);
        return -1;
    }

    return 0;
}

static int run_mr_isolation_test(config_t *config, mr_reg_record_t *records,
                                  void **buf_array, struct ibv_mr **mr_array) {
    struct ibv_device **dev_list = NULL;
    struct ibv_context *ctx = NULL;
    struct ibv_pd *pd = NULL;
    int success_count = 0;
    int fail_count = 0;

    // Allocate buffers first
    for (int i = 0; i < config->num_mr_to_register; i++) {
        buf_array[i] = malloc(config->mr_size);
        if (!buf_array[i]) {
            fprintf(stderr, "[Tenant %u] Error: Failed to allocate buffer %d\n", 
                    config->tenant_id, i);
            // Cleanup allocated buffers
            for (int j = 0; j < i; j++) {
                free(buf_array[j]);
                buf_array[j] = NULL;
            }
            return -1;
        }
    }

    // Get RDMA device
    dev_list = ibv_get_device_list(NULL);
    if (!dev_list || !dev_list[0]) {
        fprintf(stderr, "[Tenant %u] Error: No RDMA device found\n", config->tenant_id);
        goto cleanup;
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

    if (config->verbose) {
        printf("[Tenant %u] Starting to register %d MRs (size=%zu bytes)...\n",
               config->tenant_id, config->num_mr_to_register, config->mr_size);
    }

    // Register MRs and measure latency
    for (int i = 0; i < config->num_mr_to_register; i++) {
        records[i].start_time = get_time_us();
        struct ibv_mr *mr = ibv_reg_mr(pd, buf_array[i], config->mr_size, 
                                       IBV_ACCESS_LOCAL_WRITE);
        records[i].end_time = get_time_us();

        if (mr) {
            records[i].success = 1;
            records[i].error_code = 0;
            mr_array[i] = mr;
            success_count++;

            if (config->verbose && (i < 5 || i == config->num_mr_to_register - 1)) {
                printf("[Tenant %u] MR %d: Registered OK (%.2f us)\n",
                       config->tenant_id, i,
                       records[i].end_time - records[i].start_time);
            }
        } else {
            records[i].success = 0;
            records[i].error_code = errno;
            mr_array[i] = NULL;
            fail_count++;

            if (config->verbose) {
                printf("[Tenant %u] MR %d: FAILED (errno=%d, %.2f us)\n",
                       config->tenant_id, i, errno,
                       records[i].end_time - records[i].start_time);
            }
        }
    }

    if (config->verbose) {
        printf("[Tenant %u] Test complete: Success=%d, Failed=%d\n",
               config->tenant_id, success_count, fail_count);
    }

    // Cleanup MRs
    for (int i = 0; i < config->num_mr_to_register; i++) {
        if (mr_array[i]) {
            ibv_dereg_mr(mr_array[i]);
        }
    }

cleanup:
    if (pd) ibv_dealloc_pd(pd);
    if (ctx) ibv_close_device(ctx);
    if (dev_list) ibv_free_device_list(dev_list);

    // Free buffers
    for (int i = 0; i < config->num_mr_to_register; i++) {
        if (buf_array[i]) {
            free(buf_array[i]);
            buf_array[i] = NULL;
        }
    }

    return success_count;
}

static void write_results(config_t *config, mr_reg_record_t *records,
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
    fprintf(fp, "# EXP-9: MR Resource Isolation Test Results\n");
    fprintf(fp, "# Tenant ID: %u\n", config->tenant_id);
    fprintf(fp, "# MR Size: %zu bytes\n", config->mr_size);
    fprintf(fp, "# Requested MRs: %d\n", total_count);
    fprintf(fp, "# Successful: %d\n", success_count);
    fprintf(fp, "# Failed: %d\n", total_count - success_count);
    fprintf(fp, "# Success Rate: %.1f%%\n",
            total_count > 0 ? (100.0 * success_count / total_count) : 0);
    fprintf(fp, "#\n");
    fprintf(fp, "# Latency Statistics (successful registrations only):\n");
    fprintf(fp, "#   Average: %.2f us\n", avg_latency);
    fprintf(fp, "#   Min: %.2f us\n", valid_count > 0 ? min_latency : 0);
    fprintf(fp, "#   Max: %.2f us\n", valid_count > 0 ? max_latency : 0);
    fprintf(fp, "#\n");
    fprintf(fp, "# Per-MR Details:\n");
    fprintf(fp, "# MR_ID,SUCCESS,LATENCY_US,ERROR_CODE\n");

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
    printf("  MR Size:   %zu bytes\n", config->mr_size);
    printf("  Requested: %d MRs\n", total_count);
    printf("  Success:   %d MRs (%.1f%%)\n",
           success_count, total_count > 0 ? (100.0 * success_count / total_count) : 0);
    printf("  Failed:    %d MRs (%.1f%%)\n",
           total_count - success_count,
           total_count > 0 ? (100.0 * (total_count - success_count) / total_count) : 0);
    printf("  Avg Latency: %.2f us\n", avg_latency);
}

int main(int argc, char **argv) {
    config_t config;
    mr_reg_record_t records[MAX_MR];
    void *buf_array[MAX_MR];
    struct ibv_mr *mr_array[MAX_MR];

    printf("========================================\n");
    printf("EXP-9: MR Resource Isolation Test\n");
    printf("========================================\n\n");

    if (parse_args(argc, argv, &config) != 0) {
        return 1;
    }

    printf("Configuration:\n");
    printf("  Tenant ID: %u\n", config.tenant_id);
    printf("  Num MRs to register: %d\n", config.num_mr_to_register);
    printf("  MR Size: %zu bytes\n", config.mr_size);
    printf("  Output file: %s\n", config.output_file ? config.output_file : "stdout");
    printf("  Verbose: %s\n", config.verbose ? "yes" : "no");
    printf("\n");

    memset(records, 0, sizeof(records));
    memset(buf_array, 0, sizeof(buf_array));
    memset(mr_array, 0, sizeof(mr_array));

    int success_count = run_mr_isolation_test(&config, records, buf_array, mr_array);
    if (success_count < 0) {
        fprintf(stderr, "Test failed\n");
        return 1;
    }

    write_results(&config, records, success_count, config.num_mr_to_register);

    return 0;
}
