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
    
    // 解析Max QP行
    char *max_line = strstr(buffer, "Max QP:");
    if (max_line) {
        uint32_t new_max_global;
        sscanf(max_line, "Max QP: %u", &new_max_global);
        if (new_max_global > 0) {
            max_global_qp = new_max_global;
            rdma_intercept_log(LOG_LEVEL_INFO, "全局QP上限: %u", max_global_qp);
        }
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
    // 尝试获取最新数据
    if (get_global_qp_stats() < 0) {
        // 如果获取失败，返回上次的值
        rdma_intercept_log(LOG_LEVEL_WARN, "使用缓存的全局QP上限: %u", max_global_qp);
    }

    return max_global_qp;
}

// 发送QP创建事件
bool collector_send_qp_create_event(void)
{
    int temp_fd = -1;
    struct sockaddr_un addr;

    // 创建临时socket连接
    temp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (temp_fd < 0) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "无法创建collector socket: %d", errno);
        return false;
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
        return false;
    }

    // 发送QP_CREATE请求
    err = write(temp_fd, "QP_CREATE", 9);
    if (err < 0) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "无法发送QP_CREATE请求: %d", errno);
        close(temp_fd);
        return false;
    }

    // 读取响应
    char buffer[64];
    err = read(temp_fd, buffer, sizeof(buffer) - 1);
    if (err < 0) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "无法读取QP_CREATE响应: %d", errno);
        close(temp_fd);
        return false;
    }

    buffer[err] = '\0';

    // 检查响应
    bool success = (strstr(buffer, "Success") != NULL);
    if (!success) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "QP_CREATE请求被拒绝: %s", buffer);
    }

    // 关闭临时连接
    close(temp_fd);

    return success;
}

// 发送QP销毁事件
void collector_send_qp_destroy_event(void)
{
    int temp_fd = -1;
    struct sockaddr_un addr;

    // 创建临时socket连接
    temp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (temp_fd < 0) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "无法创建collector socket: %d", errno);
        return;
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
        return;
    }

    // 发送QP_DESTROY请求
    err = write(temp_fd, "QP_DESTROY", 11);
    if (err < 0) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "无法发送QP_DESTROY请求: %d", errno);
        close(temp_fd);
        return;
    }

    // 读取响应
    char buffer[64];
    err = read(temp_fd, buffer, sizeof(buffer) - 1);
    if (err < 0) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "无法读取QP_DESTROY响应: %d", errno);
        close(temp_fd);
        return;
    }

    // 关闭临时连接
    close(temp_fd);
}

// 检查全局QP使用是否达到上限
bool collector_check_global_qp_limit(void)
{
    rdma_intercept_log(LOG_LEVEL_INFO, "开始检查全局QP限制");
    
    uint32_t global_count = collector_get_global_qp_count();
    uint32_t max_global = collector_get_max_global_qp();

    rdma_intercept_log(LOG_LEVEL_INFO, "检查全局QP限制: %u/%u", global_count, max_global);

    // 如果max_global为0，表示没有设置全局QP上限，返回false
    if (max_global == 0) {
        rdma_intercept_log(LOG_LEVEL_INFO, "全局QP上限未设置，允许创建QP");
        return false;
    }

    // 如果global_count为0，且max_global大于0，允许创建QP
    if (global_count == 0 && max_global > 0) {
        rdma_intercept_log(LOG_LEVEL_INFO, "全局QP计数为0，允许创建QP");
        return false;
    }

    // 如果global_count小于max_global，允许创建QP
    if (global_count < max_global) {
        rdma_intercept_log(LOG_LEVEL_INFO, "全局QP限制未达到，允许创建QP");
        return false;
    }

    // 否则，拒绝创建QP
    rdma_intercept_log(LOG_LEVEL_ERROR, "QP creation denied: global QP limit reached: %u/%u", global_count, max_global);
    return true;
}


