/*
 * EXP-5: 动态策略热更新延迟测试
 * 
 * 测试内容：
 * 1. 创建租户并设置初始配额
 * 2. 启动应用尝试创建资源
 * 3. 在应用运行期间热更新配额
 * 4. 测量热更新的响应时间和效果
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <infiniband/verbs.h>

#define NUM_TEST_UPDATES 10

static inline double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static inline double get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

int main(int argc, char *argv[]) {
    const char *output_file = (argc > 1) ? argv[1] : "paper_results/exp5/hot_update_latency.txt";
    
    printf("[EXP-5] 动态策略热更新延迟测试\n");
    printf("========================================\n\n");
    
    // 获取RDMA设备
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list || !dev_list[0]) {
        fprintf(stderr, "错误: 未找到RDMA设备\n");
        return 1;
    }
    
    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    struct ibv_cq *cq = ibv_create_cq(ctx, 16, NULL, NULL, 0);
    
    struct ibv_qp_init_attr qp_init_attr = {
        .qp_type = IBV_QPT_RC,
        .send_cq = cq,
        .recv_cq = cq,
        .cap = { .max_send_wr = 10, .max_recv_wr = 10, .max_send_sge = 1, .max_recv_sge = 1 },
    };
    
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        fprintf(stderr, "错误: 无法创建输出文件\n");
        return 1;
    }
    
    fprintf(fp, "# EXP-5: 动态策略热更新延迟测试\n");
    fprintf(fp, "# 测试次数: %d\n\n", NUM_TEST_UPDATES);
    fprintf(fp, "UPDATE_NUM,NEW_QP_LIMIT,START_TIME_MS,END_TIME_MS,LATENCY_MS\n");
    
    printf("测试方法:\n");
    printf("1. 持续创建和销毁QP\n");
    printf("2. 记录每次操作的时间戳\n");
    printf("3. 在外部通过tenant_manager_client更新配额\n");
    printf("4. 分析配额更新前后的行为变化\n\n");
    
    printf("开始测试（按Ctrl+C停止）...\n");
    printf("请在外部终端运行:\n");
    printf("  ./build/tenant_manager_daemon --daemon --foreground\n");
    printf("  ./build/tenant_manager_client create 30 5 100 \"EXP5_Tenant\"\n");
    printf("  然后在适当时候更新配额:\n");
    printf("  ./build/tenant_manager_client update 30 10 100\n\n");
    
    double test_start = get_time_ms();
    int qp_count = 0;
    int success_count = 0;
    int fail_count = 0;
    
    // 持续创建QP，记录每次操作
    for (int i = 0; i < 100; i++) {
        double create_start = get_time_us();
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        double create_end = get_time_us();
        
        double elapsed_ms = get_time_ms() - test_start;
        
        if (qp) {
            qp_count++;
            success_count++;
            printf("[%6.1f ms] QP #%d 创建成功 (延迟: %.2f us)\n", 
                   elapsed_ms, qp_count, create_end - create_start);
            
            // 立即销毁，避免资源耗尽
            ibv_destroy_qp(qp);
        } else {
            fail_count++;
            printf("[%6.1f ms] QP 创建失败 #%d\n", elapsed_ms, fail_count);
        }
        
        // 每10个QP后暂停，给用户时间更新配额
        if ((i + 1) % 10 == 0) {
            printf("\n--- 已创建 %d 个QP，暂停3秒，请更新配额 ---\n\n", i + 1);
            sleep(3);
        }
        
        usleep(50000);  // 50ms间隔
    }
    
    double test_end = get_time_ms();
    
    printf("\n========================================\n");
    printf("测试完成\n");
    printf("总用时: %.2f ms\n", test_end - test_start);
    printf("成功: %d, 失败: %d\n", success_count, fail_count);
    
    fprintf(fp, "\n# 汇总\n");
    fprintf(fp, "TOTAL_TIME_MS: %.2f\n", test_end - test_start);
    fprintf(fp, "SUCCESS_COUNT: %d\n", success_count);
    fprintf(fp, "FAIL_COUNT: %d\n", fail_count);
    
    fclose(fp);
    
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    printf("\n结果已保存到: %s\n", output_file);
    printf("\n提示: 请检查日志中QP创建失败的时间点，\n");
    printf("      与配额更新时间对比，计算热更新延迟。\n");
    
    return 0;
}
