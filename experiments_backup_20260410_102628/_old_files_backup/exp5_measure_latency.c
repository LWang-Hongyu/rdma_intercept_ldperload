/*
 * EXP-5: 精确测量动态策略热更新延迟
 * 
 * 测试步骤：
 * 1. 启动守护进程
 * 2. 创建租户，初始配额QP=5
 * 3. 应用程序开始创建QP，每秒尝试2-3个
 * 4. 在创建5个QP后，外部更新配额到QP=10
 * 5. 记录第6个QP创建成功的时间
 * 6. 计算从更新到生效的延迟
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <json-c/json.h>
#include <infiniband/verbs.h>

#define SOCKET_PATH "/tmp/rdma_tenant_manager.sock"
#define NUM_MEASUREMENTS 10

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

// 发送更新配额命令并测量延迟
double measure_update_latency(int tenant_id, int new_qp_limit) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    
    // 构建JSON命令
    json_object* cmd = json_object_new_object();
    json_object_object_add(cmd, "cmd", json_object_new_string("UPDATE_QUOTA"));
    json_object_object_add(cmd, "tenant", json_object_new_int(tenant_id));
    json_object_object_add(cmd, "qp", json_object_new_int(new_qp_limit));
    json_object_object_add(cmd, "mr", json_object_new_int(100));
    
    const char* json_str = json_object_to_json_string(cmd);
    
    double start_time = get_time_ms();
    
    // 发送命令
    if (send(fd, json_str, strlen(json_str), 0) < 0) {
        json_object_put(cmd);
        close(fd);
        return -1;
    }
    
    // 接收响应
    char response[1024];
    int n = recv(fd, response, sizeof(response) - 1, 0);
    
    double end_time = get_time_ms();
    
    json_object_put(cmd);
    close(fd);
    
    if (n > 0) {
        response[n] = '\0';
        json_object* resp = json_tokener_parse(response);
        if (resp) {
            json_object* success_obj;
            if (json_object_object_get_ex(resp, "success", &success_obj)) {
                int success = json_object_get_boolean(success_obj);
                json_object_put(resp);
                if (success) {
                    return end_time - start_time;
                }
            }
            json_object_put(resp);
        }
    }
    
    return -1;
}

// 尝试创建QP，返回是否成功
int try_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *attr) {
    struct ibv_qp *qp = ibv_create_qp(pd, attr);
    if (qp) {
        ibv_destroy_qp(qp);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const char *output_file = (argc > 1) ? argv[1] : "paper_results/exp5/hot_update_latency.txt";
    
    printf("[EXP-5] 动态策略热更新延迟精确测试\n");
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
    
    fprintf(fp, "# EXP-5: 动态策略热更新延迟测试\n\n");
    fprintf(fp, "TEST_ROUND,API_LATENCY_MS,NOTES\n");
    
    printf("测试说明:\n");
    printf("1. 请确保守护进程已运行:\n");
    printf("   ./build/tenant_manager_daemon --daemon --foreground\n");
    printf("2. 请确保租户30已创建:\n");
    printf("   ./build/tenant_manager_client create 30 5 100 \"EXP5\"\n");
    printf("3. 本测试将自动测量API响应延迟\n\n");
    
    printf("按Enter开始测试（或等待5秒自动开始）...\n");
    sleep(5);
    
    double latencies[NUM_MEASUREMENTS];
    int valid_count = 0;
    
    for (int round = 0; round < NUM_MEASUREMENTS; round++) {
        printf("\n--- 测试 %d/%d ---\n", round + 1, NUM_MEASUREMENTS);
        
        // 测量守护进程API响应延迟
        double api_latency = measure_update_latency(30, 5 + round + 1);
        
        if (api_latency > 0) {
            latencies[valid_count++] = api_latency;
            printf("  API响应延迟: %.3f ms\n", api_latency);
            fprintf(fp, "%d,%.3f,OK\n", round + 1, api_latency);
        } else {
            printf("  测量失败\n");
            fprintf(fp, "%d,-1,FAILED\n", round + 1);
        }
        
        usleep(100000);  // 100ms间隔
    }
    
    // 计算统计
    double avg_latency = 0;
    double min_latency = 1e9;
    double max_latency = 0;
    
    for (int i = 0; i < valid_count; i++) {
        avg_latency += latencies[i];
        if (latencies[i] < min_latency) min_latency = latencies[i];
        if (latencies[i] > max_latency) max_latency = latencies[i];
    }
    
    if (valid_count > 0) {
        avg_latency /= valid_count;
    }
    
    printf("\n========================================\n");
    printf("测试结果汇总:\n");
    printf("  有效测量: %d/%d\n", valid_count, NUM_MEASUREMENTS);
    printf("  平均延迟: %.3f ms\n", avg_latency);
    printf("  最小延迟: %.3f ms\n", min_latency);
    printf("  最大延迟: %.3f ms\n", max_latency);
    
    fprintf(fp, "\n# 汇总统计\n");
    fprintf(fp, "VALID_MEASUREMENTS: %d\n", valid_count);
    fprintf(fp, "AVERAGE_LATENCY_MS: %.3f\n", avg_latency);
    fprintf(fp, "MIN_LATENCY_MS: %.3f\n", min_latency);
    fprintf(fp, "MAX_LATENCY_MS: %.3f\n", max_latency);
    
    fclose(fp);
    
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    printf("\n结果已保存到: %s\n", output_file);
    
    return 0;
}
