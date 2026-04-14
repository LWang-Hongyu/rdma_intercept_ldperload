/*
 * EXP-1: 微基准测试 - 拦截开销评估
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <infiniband/verbs.h>
#include <stdint.h>

#define NUM_ITERATIONS 1000
#define NUM_WARMUP 100

typedef struct {
    double qp_create[NUM_ITERATIONS];
    double qp_destroy[NUM_ITERATIONS];
    double mr_reg[NUM_ITERATIONS];
    double mr_dereg[NUM_ITERATIONS];
} latency_data_t;

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
    int valid_count = 0;
    
    stats->min = 1e9;
    stats->max = 0;
    
    for (int i = 0; i < count; i++) {
        if (data[i] > 0) {
            sum += data[i];
            sum_sq += data[i] * data[i];
            if (data[i] < stats->min) stats->min = data[i];
            if (data[i] > stats->max) stats->max = data[i];
            valid_count++;
        }
    }
    
    if (valid_count == 0) {
        memset(stats, 0, sizeof(stats_t));
        return;
    }
    
    stats->mean = sum / valid_count;
    stats->std = sqrt(sum_sq / valid_count - stats->mean * stats->mean);
    
    // Sort for percentiles
    qsort(data, count, sizeof(double), compare_double);
    
    int valid_idx = 0;
    double sorted_valid[NUM_ITERATIONS];
    for (int i = 0; i < count; i++) {
        if (data[i] > 0) {
            sorted_valid[valid_idx++] = data[i];
        }
    }
    
    if (valid_idx > 0) {
        stats->p50 = sorted_valid[(int)(valid_idx * 0.5)];
        stats->p95 = sorted_valid[(int)(valid_idx * 0.95)];
        stats->p99 = sorted_valid[(int)(valid_idx * 0.99)];
    }
}

int main(int argc, char *argv[]) {
    const char *output_file = (argc > 1) ? argv[1] : "/tmp/exp1_result.txt";
    
    printf("[EXP-1] 微基准测试开始\n");
    printf("  迭代次数: %d\n", NUM_ITERATIONS);
    printf("  预热次数: %d\n", NUM_WARMUP);
    
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
    
    latency_data_t data;
    memset(&data, 0, sizeof(data));
    
    // 预热
    printf("  预热中...\n");
    for (int i = 0; i < NUM_WARMUP; i++) {
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        if (qp) ibv_destroy_qp(qp);
    }
    
    // 测试QP创建延迟
    printf("  测试QP创建延迟...\n");
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        double start = get_time_us();
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        double end = get_time_us();
        
        if (qp) {
            data.qp_create[i] = end - start;
            ibv_destroy_qp(qp);
        } else {
            data.qp_create[i] = -1;
        }
    }
    
    // 测试QP创建+销毁完整周期
    printf("  测试QP完整周期...\n");
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        if (!qp) continue;
        
        double start = get_time_us();
        ibv_destroy_qp(qp);
        double end = get_time_us();
        
        data.qp_destroy[i] = end - start;
    }
    
    // 测试MR注册延迟
    printf("  测试MR注册延迟...\n");
    char *buf = malloc(4096);
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        double start = get_time_us();
        struct ibv_mr *mr = ibv_reg_mr(pd, buf, 4096, IBV_ACCESS_LOCAL_WRITE);
        double end = get_time_us();
        
        if (mr) {
            data.mr_reg[i] = end - start;
            ibv_dereg_mr(mr);
        } else {
            data.mr_reg[i] = -1;
        }
    }
    free(buf);
    
    // 计算统计
    stats_t qp_create_stats, qp_destroy_stats, mr_reg_stats;
    calculate_stats(data.qp_create, NUM_ITERATIONS, &qp_create_stats);
    calculate_stats(data.qp_destroy, NUM_ITERATIONS, &qp_destroy_stats);
    calculate_stats(data.mr_reg, NUM_ITERATIONS, &mr_reg_stats);
    
    // 输出结果
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        fprintf(stderr, "错误: 无法创建输出文件\n");
        return 1;
    }
    
    fprintf(fp, "# EXP-1: 微基准测试结果\n");
    fprintf(fp, "# 时间: %s", ctime(&(time_t){time(NULL)}));
    fprintf(fp, "# 迭代次数: %d\n\n", NUM_ITERATIONS);
    
    fprintf(fp, "## QP_CREATE_LATENCY (us)\n");
    fprintf(fp, "MEAN: %.3f\n", qp_create_stats.mean);
    fprintf(fp, "STD: %.3f\n", qp_create_stats.std);
    fprintf(fp, "MIN: %.3f\n", qp_create_stats.min);
    fprintf(fp, "MAX: %.3f\n", qp_create_stats.max);
    fprintf(fp, "P50: %.3f\n", qp_create_stats.p50);
    fprintf(fp, "P95: %.3f\n", qp_create_stats.p95);
    fprintf(fp, "P99: %.3f\n", qp_create_stats.p99);
    
    fprintf(fp, "\n## QP_DESTROY_LATENCY (us)\n");
    fprintf(fp, "MEAN: %.3f\n", qp_destroy_stats.mean);
    fprintf(fp, "P50: %.3f\n", qp_destroy_stats.p50);
    fprintf(fp, "P95: %.3f\n", qp_destroy_stats.p95);
    
    fprintf(fp, "\n## MR_REG_LATENCY (us)\n");
    fprintf(fp, "MEAN: %.3f\n", mr_reg_stats.mean);
    fprintf(fp, "STD: %.3f\n", mr_reg_stats.std);
    fprintf(fp, "P50: %.3f\n", mr_reg_stats.p50);
    fprintf(fp, "P95: %.3f\n", mr_reg_stats.p95);
    fprintf(fp, "P99: %.3f\n", mr_reg_stats.p99);
    
    fclose(fp);
    
    // 屏幕输出
    printf("\n=== 测试结果 ===\n");
    printf("QP_CREATE_LATENCY_MEAN: %.3f us\n", qp_create_stats.mean);
    printf("QP_CREATE_LATENCY_P95: %.3f us\n", qp_create_stats.p95);
    printf("QP_CREATE_LATENCY_P99: %.3f us\n", qp_create_stats.p99);
    printf("MR_REG_LATENCY_MEAN: %.3f us\n", mr_reg_stats.mean);
    printf("MR_REG_LATENCY_P95: %.3f us\n", mr_reg_stats.p95);
    printf("结果已保存到: %s\n", output_file);
    
    // 清理
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    return 0;
}
