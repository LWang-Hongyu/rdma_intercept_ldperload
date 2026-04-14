/*
 * EXP-3 V2: 可扩展性测试（严谨版本）
 * 
 * 设计原则:
 * 1. 固定每租户QP数 = 10，只改变租户数
 * 2. 每个租户先预热1个QP，消除冷启动影响
 * 3. 测试两组: 小配额(5) vs 大配额(50) vs 无拦截
 * 
 * 用法:
 *   ./exp3_v2 --tenants=10 --warmup=1 --output=results.csv
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <infiniband/verbs.h>

#define MAX_TENANTS 200
#define QP_PER_TENANT 10  // 固定每租户QP数

typedef struct {
    int tenant_id;
    int num_qp;
    int success;
    double total_time_us;
    double avg_latency_us;
    double p50_latency_us;
    double p99_latency_us;
    double latencies[QP_PER_TENANT];
} tenant_result_t;

static inline double get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

int cmp_double(const void *a, const void *b) {
    double da = *(double*)a;
    double db = *(double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

void tenant_process(int tenant_id, int num_qp, int warmup, int pipe_fd) {
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list || !dev_list[0]) {
        fprintf(stderr, "Tenant %d: No IB device\n", tenant_id);
        exit(1);
    }
    
    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    struct ibv_cq *cq = ibv_create_cq(ctx, num_qp + 10, NULL, NULL, 0);
    
    tenant_result_t result = {0};
    result.tenant_id = tenant_id;
    result.num_qp = num_qp;
    
    // 预热阶段（创建并销毁warmup个QP，消除冷启动）
    if (warmup > 0) {
        for (int i = 0; i < warmup; i++) {
            struct ibv_qp_init_attr qp_attr = {
                .send_cq = cq, .recv_cq = cq, .qp_type = IBV_QPT_RC,
                .cap = { .max_send_wr = 1, .max_recv_wr = 1, 
                        .max_send_sge = 1, .max_recv_sge = 1 }
            };
            struct ibv_qp *qp = ibv_create_qp(pd, &qp_attr);
            if (qp) ibv_destroy_qp(qp);
        }
    }
    
    // 正式测试阶段
    double start = get_time_us();
    
    for (int i = 0; i < num_qp; i++) {
        double qp_start = get_time_us();
        
        struct ibv_qp_init_attr qp_attr = {
            .send_cq = cq, .recv_cq = cq, .qp_type = IBV_QPT_RC,
            .cap = { .max_send_wr = 1, .max_recv_wr = 1,
                    .max_send_sge = 1, .max_recv_sge = 1 }
        };
        
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_attr);
        double qp_end = get_time_us();
        
        if (qp) {
            result.latencies[result.success] = qp_end - qp_start;
            result.success++;
            ibv_destroy_qp(qp);
        }
    }
    
    double end = get_time_us();
    result.total_time_us = end - start;
    
    // 计算统计值
    if (result.success > 0) {
        qsort(result.latencies, result.success, sizeof(double), cmp_double);
        result.avg_latency_us = result.total_time_us / result.success;
        result.p50_latency_us = result.latencies[result.success / 2];
        result.p99_latency_us = result.latencies[(int)(result.success * 0.99)];
    }
    
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    write(pipe_fd, &result, sizeof(result));
    close(pipe_fd);
    exit(0);
}

int main(int argc, char *argv[]) {
    int num_tenants = 10;
    int warmup = 1;
    const char *output_file = "results.csv";
    
    static struct option long_options[] = {
        {"tenants", required_argument, 0, 't'},
        {"warmup", required_argument, 0, 'w'},
        {"output", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "t:w:o:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 't': num_tenants = atoi(optarg); break;
            case 'w': warmup = atoi(optarg); break;
            case 'o': output_file = optarg; break;
            case 'h':
                printf("Usage: %s [options]\n", argv[0]);
                printf("Options:\n");
                printf("  -t, --tenants=N     Number of tenants (default: 10)\n");
                printf("  -w, --warmup=N      Warmup QPs per tenant (default: 1)\n");
                printf("  -o, --output=FILE   Output CSV file\n");
                return 0;
            default: return 1;
        }
    }
    
    if (num_tenants > MAX_TENANTS) {
        fprintf(stderr, "Max tenants: %d\n", MAX_TENANTS);
        return 1;
    }
    
    printf("========================================\n");
    printf("EXP-3 V2: Scalability Test\n");
    printf("========================================\n");
    printf("Tenants:        %d\n", num_tenants);
    printf("QP per tenant:  %d (fixed)\n", QP_PER_TENANT);
    printf("Total QPs:      %d\n", num_tenants * QP_PER_TENANT);
    printf("Warmup QPs:     %d per tenant\n", warmup);
    printf("========================================\n\n");
    
    int pipes[MAX_TENANTS][2];
    pid_t pids[MAX_TENANTS];
    
    double test_start = get_time_us();
    
    // 创建所有租户进程
    for (int i = 0; i < num_tenants; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            return 1;
        }
        
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        
        if (pid == 0) {
            close(pipes[i][0]);
            tenant_process(i + 1, QP_PER_TENANT, warmup, pipes[i][1]);
        }
        
        pids[i] = pid;
        close(pipes[i][1]);
    }
    
    // 收集结果
    tenant_result_t results[MAX_TENANTS];
    int total_success = 0;
    double total_time = 0;
    double all_latencies[MAX_TENANTS * QP_PER_TENANT];
    int lat_count = 0;
    
    for (int i = 0; i < num_tenants; i++) {
        read(pipes[i][0], &results[i], sizeof(tenant_result_t));
        close(pipes[i][0]);
        waitpid(pids[i], NULL, 0);
        
        total_success += results[i].success;
        total_time += results[i].total_time_us;
        
        for (int j = 0; j < results[i].success && lat_count < MAX_TENANTS * QP_PER_TENANT; j++) {
            all_latencies[lat_count++] = results[i].latencies[j];
        }
    }
    
    double test_end = get_time_us();
    double test_duration = (test_end - test_start) / 1000000.0;
    
    // 全局统计
    qsort(all_latencies, lat_count, sizeof(double), cmp_double);
    double global_avg = total_time / total_success;
    double global_p50 = all_latencies[lat_count / 2];
    double global_p99 = all_latencies[(int)(lat_count * 0.99)];
    double throughput = total_success / test_duration;
    
    printf("\n========================================\n");
    printf("Results Summary\n");
    printf("========================================\n");
    printf("Total QPs created:   %d/%d\n", total_success, num_tenants * QP_PER_TENANT);
    printf("Test duration:       %.3f sec\n", test_duration);
    printf("Throughput:          %.0f ops/sec\n", throughput);
    printf("Avg latency:         %.1f us\n", global_avg);
    printf("P50 latency:         %.1f us\n", global_p50);
    printf("P99 latency:         %.1f us\n", global_p99);
    printf("========================================\n");
    
    // 保存CSV
    FILE *fp = fopen(output_file, "w");
    if (fp) {
        fprintf(fp, "tenant_id,num_qp,success,total_time_us,avg_latency_us,p50_latency_us,p99_latency_us\n");
        for (int i = 0; i < num_tenants; i++) {
            fprintf(fp, "%d,%d,%d,%.1f,%.1f,%.1f,%.1f\n",
                results[i].tenant_id, results[i].num_qp, results[i].success,
                results[i].total_time_us, results[i].avg_latency_us,
                results[i].p50_latency_us, results[i].p99_latency_us);
        }
        fclose(fp);
        printf("Results saved to: %s\n", output_file);
    }
    
    return 0;
}
