#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rdma_intercept.h"

// 前向声明
uint32_t collector_get_global_qp_count(void);
uint32_t collector_get_max_global_qp(void);
bool collector_check_global_qp_limit(void);

// 全局变量
static uint32_t last_adjust_time = 0;
static uint32_t local_qp_limit = 100; // 默认本地QP限制

// 调整本地QP限制
static void adjust_local_qp_limit(void)
{
    uint32_t current_time = time(NULL);
    
    // 限制调整频率，避免过于频繁的调整
    if (current_time - last_adjust_time < 5) {
        return;
    }
    
    last_adjust_time = current_time;
    
    // 获取全局QP使用情况
    uint32_t global_count = collector_get_global_qp_count();
    uint32_t max_global = collector_get_max_global_qp();
    
    // 计算全局QP使用率
    float global_usage = (float)global_count / max_global;
    
    // 根据全局使用率调整本地限制
    if (global_usage < 0.3) {
        // 全局资源充足，使用较大的本地限制
        local_qp_limit = 200;
    } else if (global_usage < 0.7) {
        // 全局资源适中，使用默认本地限制
        local_qp_limit = 100;
    } else {
        // 全局资源紧张，使用较小的本地限制
        local_qp_limit = 50;
    }
    
    // 从环境变量读取本地QP限制（优先级最高）
    const char *local_limit_str = getenv("RDMA_INTERCEPT_LOCAL_QP_LIMIT");
    if (local_limit_str) {
        local_qp_limit = atoi(local_limit_str);
    }
    
    rdma_intercept_log(LOG_LEVEL_INFO, "调整本地QP限制为: %u (全局使用率: %.2f%%)", 
                     local_qp_limit, global_usage * 100);
}

// 检查QP创建是否符合动态策略
bool check_dynamic_qp_policy(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
    (void)pd;  // 未使用的参数
    (void)qp_init_attr;  // 未使用的参数
    
    // 调整本地QP限制
    adjust_local_qp_limit();
    
    // 检查全局QP限制
    if (collector_check_global_qp_limit()) {
        return false;
    }
    
    // 检查本地QP限制
    if (g_intercept_state.qp_count >= local_qp_limit) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "本地QP限制已达到: %u/%u", 
                         g_intercept_state.qp_count, local_qp_limit);
        return false;
    }
    
    return true;
}

// 获取当前本地QP限制
uint32_t get_current_local_qp_limit(void)
{
    adjust_local_qp_limit();
    return local_qp_limit;
}

// 初始化动态策略
void init_dynamic_policy(void)
{
    rdma_intercept_log(LOG_LEVEL_INFO, "初始化动态QP策略模块");
    adjust_local_qp_limit();
}
