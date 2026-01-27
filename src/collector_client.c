#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "rdma_intercept.h"

// 全局变量
static uint32_t global_qp_count = 0;
static uint32_t max_global_qp = 1000; // 默认全局QP上限

// 从数据收集服务获取全局QP统计
static int get_global_qp_stats(void)
{
    char buffer[1024];
    int n;
    int temp_fd = -1;
    struct sockaddr_un addr;

    // 创建临时socket连接
    temp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (temp_fd < 0) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "无法创建collector socket: %d", errno);
        return -1;
    }

    // 准备地址
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/rdma_collector.sock", sizeof(addr.sun_path) - 1);

    // 连接到服务
    int err = connect(temp_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        rdma_intercept_log(LOG_LEVEL_WARN, "无法连接到数据收集服务: %d", errno);
        close(temp_fd);
        return -1;
    }

    // 发送请求
    n = write(temp_fd, "GET_STATS", 9);
    if (n < 0) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "无法发送请求: %d", errno);
        close(temp_fd);
        return -1;
    }

    // 读取响应
    n = read(temp_fd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "无法读取响应: %d", errno);
        close(temp_fd);
        return -1;
    }

    buffer[n] = '\0';

    // 解析响应
    char *total_line = strstr(buffer, "Total QP:");
    if (total_line) {
        sscanf(total_line, "Total QP: %u", &global_qp_count);
        rdma_intercept_log(LOG_LEVEL_INFO, "全局QP计数: %u", global_qp_count);
    }

    // 关闭临时连接
    close(temp_fd);

    return 0;
}

// 获取全局QP使用情况
uint32_t collector_get_global_qp_count(void)
{
    // 尝试获取最新数据
    if (get_global_qp_stats() < 0) {
        // 如果获取失败，返回上次的值
        rdma_intercept_log(LOG_LEVEL_WARN, "使用缓存的全局QP计数: %u", global_qp_count);
    }

    return global_qp_count;
}

// 获取全局QP上限
uint32_t collector_get_max_global_qp(void)
{
    // 从环境变量读取全局QP上限
    const char *max_qp_str = getenv("RDMA_INTERCEPT_MAX_GLOBAL_QP");
    if (max_qp_str) {
        max_global_qp = atoi(max_qp_str);
    }

    return max_global_qp;
}

// 检查全局QP使用是否达到上限
bool collector_check_global_qp_limit(void)
{
    uint32_t global_count = collector_get_global_qp_count();
    uint32_t max_global = collector_get_max_global_qp();

    if (global_count >= max_global) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "全局QP上限已达到: %u/%u", global_count, max_global);
        return true;
    }

    return false;
}


