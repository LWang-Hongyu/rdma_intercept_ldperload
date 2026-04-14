/*
 * EXP-1: 微基准测试 - 拦截开销评估 (v2)
 * 
 * 修复冷启动问题：先创建预热QP保持"热"状态，再测试正常创建的延迟
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <infiniband/verbs.h>
#include <stdint.h>
#include <math.h>

#define NUM_HOT_QPS 10        // 预热QP数量（保持热状态）
#define NUM_ITERATIONS 1000   // 测试迭代次数
#define TEST_MR_SIZE 4096     // MR测试大小

typedef struct {
    double latencies[NUM_ITERATIONS];
    int count;
} latency_series_t;

typedef struct {
    double mean;
    double std;
    double min;
    double max;
    double p50;
    double p95;
    double p99;
} stats_t;

static inline double get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static int compare_double(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

void calculate_stats(double *data, int count, stats_t *stats) {
    double sum = 0, sum_sq = 0;
    
    stats->min = 1e9;
    stats->max = 0;
    
    for (int i = 0; i < count; i++) {
        sum += data[i];
        sum_sq += data[i] * data[i];
        if (data[i] < stats->min) stats->min = data[i];
        if (data[i] > stats->max) stats->max = data[i];
    }
    
    stats->mean = sum / count;
    stats->std = sqrt(sum_sq / count - stats->mean * stats->mean);
    
    // Sort for percentiles
    qsort(data, count, sizeof(double), compare_double);
    
    stats->p50 = data[(int)(count * 0.5)];
    stats->p95 = data[(int)(count * 0.95)];
    stats->p99 = data[(int)(count * 0.99)];
}

int main(int argc, char *argv[]) {
    const char *output_file = (argc > 1) ? argv[1] : "paper_results/exp1/microbenchmark_v2.txt";
    
    printf("[EXP-1 v2] 微基准测试开始\n");
    printf("========================================\n");
    printf("  预热QP数: %d (保持热状态)\n", NUM_HOT_QPS);
    printf("  测试迭代: %d\n", NUM_ITERATIONS);
    
    // 获取RDMA设备
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list || !dev_list[0]) {
        fprintf(stderr, "错误: 未找到RDMA设备\n");
        return 1;
    }
    
    printf("  使用设备: %s\n", ibv_get_device_name(dev_list[0]));
    
    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    struct ibv_cq *cq = ibv_create_cq(ctx, 16, NULL, NULL, 0);
    
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
    
    // 存储预热QP和测试QP
    struct ibv_qp *hot_qps[NUM_HOT_QPS];
    struct ibv_qp *test_qps[NUM_ITERATIONS];
    
    memset(hot_qps, 0, sizeof(hot_qps));
    memset(test_qps, 0, sizeof(test_qps));
    
    // ========== 阶段1: 创建预热QP（进入热状态）==========
    printf("\n[阶段1] 创建%d个预热QP...\n", NUM_HOT_QPS);
    double cold_start_latencies[NUM_HOT_QPS];
    
    for (int i = 0; i < NUM_HOT_QPS; i++) {
        double start = get_time_us();
        hot_qps[i] = ibv_create_qp(pd, &qp_init_attr);
        double end = get_time_us();
        
        if (!hot_qps[i]) {
            fprintf(stderr, "错误: 预热QP %d 创建失败\n", i);
            return 1;
        }
        cold_start_latencies[i] = end - start;
    }
    
    printf("  冷启动延迟: 1st=%.1f us, 10th=%.1f us\n", 
           cold_start_latencies[0], cold_start_latencies[NUM_HOT_QPS-1]);
    
    // ========== 阶段2: 测试正常QP创建延迟 ==========
    printf("\n[阶段2] 测试%d次正常QP创建...\n", NUM_ITERATIONS);
    latency_series_t qp_create_data = {0};
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        double start = get_time_us();
        test_qps[i] = ibv_create_qp(pd, &qp_init_attr);
        double end = get_time_us();
        
        if (!test_qps[i]) {
            fprintf(stderr, "错误: 测试QP %d 创建失败\n", i);
            break;
        }
        qp_create_data.latencies[qp_create_data.count++] = end - start;
    }
    
    printf("  完成%d次QP创建测试\n", qp_create_data.count);
    
    // ========== 阶段3: 测试MR注册延迟 ==========
    printf("\n[阶段3] 测试%d次MR注册...\n", NUM_ITERATIONS);
    latency_series_t mr_reg_data = {0};
    char *buf = aligned_alloc(4096, TEST_MR_SIZE);
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        double start = get_time_us();
        struct ibv_mr *mr = ibv_reg_mr(pd, buf, TEST_MR_SIZE, IBV_ACCESS_LOCAL_WRITE);
        double end = get_time_us();
        
        if (!mr) {
            fprintf(stderr, "错误: MR %d 注册失败\n", i);
            break;
        }
        mr_reg_data.latencies[mr_reg_data.count++] = end - start;
        ibv_dereg_mr(mr);
    }
    
    free(buf);
    printf("  完成%d次MR注册测试\n", mr_reg_data.count);
    
    // ========== 阶段4: 计算统计 ==========
    stats_t qp_stats, mr_stats;
    calculate_stats(qp_create_data.latencies, qp_create_data.count, &qp_stats);
    calculate_stats(mr_reg_data.latencies, mr_reg_data.count, &mr_stats);
    
    // ========== 阶段5: 保存结果 ==========
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        fprintf(stderr, "错误: 无法创建输出文件\n");
        return 1;
    }
    
    fprintf(fp, "# EXP-1 v2: 微基准测试结果 (排除冷启动)\n");
    fprintf(fp, "# 时间: %s", ctime(&(time_t){time(NULL)}));
    fprintf(fp, "# 预热QP数: %d\n", NUM_HOT_QPS);
    fprintf(fp, "# 测试迭代: %d\n\n", NUM_ITERATIONS);
    
    fprintf(fp, "## COLD_START_QP_LATENCY (us)\n");
    fprintf(fp, "1ST: %.3f\n", cold_start_latencies[0]);
    fprintf(fp, "10TH: %.3f\n", cold_start_latencies[9]);
    
    fprintf(fp, "\n## NORMAL_QP_CREATE_LATENCY (us)\n");
    fprintf(fp, "MEAN: %.3f\n", qp_stats.mean);
    fprintf(fp, "STD: %.3f\n", qp_stats.std);
    fprintf(fp, "MIN: %.3f\n", qp_stats.min);
    fprintf(fp, "MAX: %.3f\n", qp_stats.max);
    fprintf(fp, "P50: %.3f\n", qp_stats.p50);
    fprintf(fp, "P95: %.3f\n", qp_stats.p95);
    fprintf(fp, "P99: %.3f\n", qp_stats.p99);
    
    fprintf(fp, "\n## MR_REG_LATENCY (us)\n");
    fprintf(fp, "MEAN: %.3f\n", mr_stats.mean);
    fprintf(fp, "STD: %.3f\n", mr_stats.std);
    fprintf(fp, "MIN: %.3f\n", mr_stats.min);
    fprintf(fp, "MAX: %.3f\n", mr_stats.max);
    fprintf(fp, "P50: %.3f\n", mr_stats.p50);
    fprintf(fp, "P95: %.3f\n", mr_stats.p95);
    fprintf(fp, "P99: %.3f\n", mr_stats.p99);
    
    // 原始数据
    fprintf(fp, "\n## RAW_QP_CREATE_DATA\n");
    for (int i = 0; i < qp_create_data.count && i < 100; i++) {
        fprintf(fp, "%d,%.3f\n", i, qp_create_data.latencies[i]);
    }
    
    fprintf(fp, "\n## RAW_MR_REG_DATA\n");
    for (int i = 0; i < mr_reg_data.count && i < 100; i++) {
        fprintf(fp, "%d,%.3f\n", i, mr_reg_data.latencies[i]);
    }
    
    fclose(fp);
    
    // 屏幕输出
    printf("\n========================================\n");
    printf("测试结果 (排除冷启动)\n");
    printf("========================================\n");
    printf("冷启动延迟:\n");
    printf("  第1个QP: %.1f us (%.1f ms)\n", cold_start_latencies[0], cold_start_latencies[0]/1000);
    printf("  第10个QP: %.1f us (%.1f ms)\n", cold_start_latencies[9], cold_start_latencies[9]/1000);
    printf("\n正常QP创建延迟:\n");
    printf("  MEAN:  %.1f us\n", qp_stats.mean);
    printf("  P50:   %.1f us\n", qp_stats.p50);
    printf("  P95:   %.1f us\n", qp_stats.p95);
    printf("  P99:   %.1f us\n", qp_stats.p99);
    printf("\nMR注册延迟:\n");
    printf("  MEAN:  %.1f us\n", mr_stats.mean);
    printf("  P50:   %.1f us\n", mr_stats.p50);
    printf("  P95:   %.1f us\n", mr_stats.p95);
    printf("\n结果已保存到: %s\n", output_file);
    
    // 清理
    for (int i = 0; i < NUM_HOT_QPS; i++) {
        if (hot_qps[i]) ibv_destroy_qp(hot_qps[i]);
    }
    for (int i = 0; i < qp_create_data.count; i++) {
        if (test_qps[i]) ibv_destroy_qp(test_qps[i]);
    }
    
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    return 0;
}
