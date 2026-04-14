/*
 * EXP-5: 动态策略热更新效果验证
 * 
 * 实验设计：
 * 1. 创建租户，初始配额 QP=5
 * 2. 应用程序以固定间隔（如500ms）尝试创建QP
 * 3. 记录每个QP创建的时间戳和结果
 * 4. 在第N秒时，通过外部命令热更新配额到 QP=10
 * 5. 验证：更新后，应用程序可以继续创建更多QP
 * 6. 绘制时间轴图，显示配额更新前后的成功/失败情况
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <infiniband/verbs.h>

#define MAX_QP 20
#define CREATE_INTERVAL_MS 500  // 每500ms尝试创建一个QP

typedef struct {
    int qp_id;
    double timestamp_ms;  // 相对于实验开始的时间
    int success;          // 1=成功, 0=失败
    double latency_us;    // 创建延迟
} qp_attempt_t;

static volatile int running = 1;
static double experiment_start;

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

void signal_handler(int sig) {
    running = 0;
}

int main(int argc, char *argv[]) {
    const char *output_file = (argc > 1) ? argv[1] : "paper_results/exp5/dynamic_policy_effect.txt";
    int initial_quota = 5;
    int updated_quota = 10;
    int update_at_qp = 7;  // 在第7次尝试时更新配额
    
    printf("[EXP-5] 动态策略热更新效果验证\n");
    printf("========================================\n");
    printf("实验设计:\n");
    printf("  初始配额: %d QP\n", initial_quota);
    printf("  更新后配额: %d QP\n", updated_quota);
    printf("  更新时机: 第%d次创建尝试时\n", update_at_qp);
    printf("  创建间隔: %d ms\n", CREATE_INTERVAL_MS);
    printf("  总计尝试: %d 次\n\n", MAX_QP);
    
    printf("请按以下步骤操作:\n");
    printf("1. 启动守护进程:\n");
    printf("   ./build/tenant_manager_daemon --daemon --foreground\n");
    printf("2. 创建租户（初始配额=5）:\n");
    printf("   ./build/tenant_manager_client create 50 %d 100 \"EXP5\"\n", initial_quota);
    printf("3. 运行本实验:\n");
    printf("   export LD_PRELOAD=./build/librdma_intercept.so\n");
    printf("   export RDMA_INTERCEPT_ENABLE=1\n");
    printf("   export RDMA_TENANT_ID=50\n");
    printf("   export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1\n");
    printf("   ./build/exp5_dynamic_test\n");
    printf("4. 在第7次尝试时（约3.5秒），在另一个终端执行:\n");
    printf("   ./build/tenant_manager_client update 50 %d 100\n\n", updated_quota);
    
    // 检查是否是非交互模式（通过环境变量或自动输入）
    const char* auto_mode = getenv("EXP5_AUTO_MODE");
    if (!auto_mode) {
        printf("按Enter开始实验（或设置 EXP5_AUTO_MODE=1 跳过）...\n");
        getchar();
    } else {
        printf("自动模式，跳过确认...\n");
        sleep(1);
    }
    
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
    
    qp_attempt_t attempts[MAX_QP];
    struct ibv_qp *created_qps[MAX_QP];  // 保存创建的QP指针
    int qp_count = 0;
    memset(created_qps, 0, sizeof(created_qps));
    
    experiment_start = get_time_ms();
    signal(SIGINT, signal_handler);
    
    printf("实验开始! 时间戳基于实验开始时间\n");
    printf("%-10s %-10s %-10s %-12s %s\n", "尝试#", "时间(ms)", "结果", "延迟(us)", "备注");
    printf("------------------------------------------------------------\n");
    
    for (int i = 0; i < MAX_QP && running; i++) {
        double timestamp = get_time_ms() - experiment_start;
        
        double create_start = get_time_us();
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        double create_end = get_time_us();
        
        attempts[i].qp_id = i;
        attempts[i].timestamp_ms = timestamp;
        attempts[i].success = (qp != NULL);
        attempts[i].latency_us = create_end - create_start;
        
        const char* result_str = qp ? "SUCCESS" : "FAILED";
        const char* note = "";
        
        if (i == update_at_qp - 1) {
            note = "<- 此时应更新配额!";
        } else if (i >= update_at_qp && qp) {
            note = "<- 热更新后成功!";
        } else if (i >= initial_quota && !qp) {
            note = "<- 配额限制";
        }
        
        printf("%-10d %10.1f %-10s %12.1f %s\n", 
               i + 1, timestamp, result_str, attempts[i].latency_us, note);
        
        if (qp) {
            created_qps[qp_count] = qp;  // 保存QP指针
            qp_count++;
            // 不立即销毁QP，让它们保持存在以测试配额限制
        }
        
        usleep(CREATE_INTERVAL_MS * 1000);  // 等待下一个间隔
    }
    
    double total_time = get_time_ms() - experiment_start;
    
    // 统计（在文件操作外进行）
    int success_before = 0, success_after = 0;
    int fail_before = 0, fail_after = 0;
    
    for (int i = 0; i < MAX_QP; i++) {
        if (i < update_at_qp - 1) {
            if (attempts[i].success) success_before++;
            else fail_before++;
        } else {
            if (attempts[i].success) success_after++;
            else fail_after++;
        }
    }
    
    // 保存结果
    FILE *fp = fopen(output_file, "w");
    if (fp) {
        fprintf(fp, "# EXP-5: 动态策略热更新效果验证\n");
        fprintf(fp, "# 初始配额: %d QP\n", initial_quota);
        fprintf(fp, "# 更新后配额: %d QP\n", updated_quota);
        fprintf(fp, "# 更新时机: 第%d次尝试\n", update_at_qp);
        fprintf(fp, "# 创建间隔: %d ms\n\n", CREATE_INTERVAL_MS);
        
        fprintf(fp, "# 原始数据\n");
        fprintf(fp, "ATTEMPT_ID,TIMESTAMP_MS,SUCCESS,LATENCY_US\n");
        
        for (int i = 0; i < MAX_QP; i++) {
            fprintf(fp, "%d,%.3f,%d,%.3f\n",
                    attempts[i].qp_id + 1,
                    attempts[i].timestamp_ms,
                    attempts[i].success,
                    attempts[i].latency_us);
        }
        
        fprintf(fp, "\n# 统计汇总\n");
        fprintf(fp, "TOTAL_ATTEMPTS: %d\n", MAX_QP);
        fprintf(fp, "TOTAL_SUCCESS: %d\n", qp_count);
        fprintf(fp, "TOTAL_FAILED: %d\n", MAX_QP - qp_count);
        fprintf(fp, "SUCCESS_RATE: %.1f%%\n", 100.0 * qp_count / MAX_QP);
        fprintf(fp, "\n# 热更新前后对比\n");
        fprintf(fp, "BEFORE_UPDATE_ATTEMPTS: %d\n", update_at_qp - 1);
        fprintf(fp, "BEFORE_UPDATE_SUCCESS: %d\n", success_before);
        fprintf(fp, "AFTER_UPDATE_ATTEMPTS: %d\n", MAX_QP - update_at_qp + 1);
        fprintf(fp, "AFTER_UPDATE_SUCCESS: %d\n", success_after);
        
        fclose(fp);
        printf("\n结果已保存到: %s\n", output_file);
    }
    
    printf("\n========================================\n");
    printf("实验完成!\n");
    printf("总用时: %.1f ms\n", total_time);
    printf("成功创建: %d/%d (%.1f%%)\n", qp_count, MAX_QP, 100.0 * qp_count / MAX_QP);
    
    // 验证热更新效果
    if (success_after > 0) {
        printf("\n✓ 热更新验证成功!\n");
        printf("  更新后成功创建了 %d 个QP\n", success_after);
    } else {
        printf("\n✗ 热更新可能未生效\n");
        printf("  更新后没有成功创建更多QP\n");
        printf("  请检查是否在正确时间执行了配额更新命令\n");
    }
    
    // 清理所有创建的QP
    printf("\n清理 %d 个QP...\n", qp_count);
    for (int i = 0; i < qp_count; i++) {
        if (created_qps[i]) {
            ibv_destroy_qp(created_qps[i]);
        }
    }
    
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    return 0;
}
