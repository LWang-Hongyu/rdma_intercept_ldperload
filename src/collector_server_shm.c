/*
 * collector_server_shm.c
 * 基于共享内存的资源收集服务器
 * 从eBPF maps读取数据并同步到共享内存
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <linux/bpf.h>
#include <sys/syscall.h>
#include <time.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "shm/shared_memory.h"
#include "ebpf/ebpf_monitor_shm.h"

// 全局变量
static volatile int running = 1;
static uint32_t max_global_qp = 10; // 默认全局QP上限
static pthread_mutex_t qp_count_mutex = PTHREAD_MUTEX_INITIALIZER;

// 内存资源全局变量
static uint64_t max_global_memory = 1024ULL * 1024ULL * 1024ULL * 10; // 默认全局内存上限10GB
static pthread_mutex_t memory_count_mutex = PTHREAD_MUTEX_INITIALIZER;

// eBPF相关变量
static int process_resources_map_fd = -1;
static int global_resources_map_fd = -1;
static pthread_t sync_thread;
static pthread_mutex_t ebpf_sync_mutex = PTHREAD_MUTEX_INITIALIZER;

// 获取全局QP上限
static void update_max_global_qp(void) {
    // 从环境变量读取全局QP上限
    const char *max_qp_str = getenv("RDMA_INTERCEPT_MAX_GLOBAL_QP");
    if (max_qp_str) {
        uint32_t new_max = atoi(max_qp_str);
        if (new_max > 0) {
            pthread_mutex_lock(&qp_count_mutex);
            max_global_qp = new_max;
            pthread_mutex_unlock(&qp_count_mutex);
            printf("更新全局QP上限为: %u\n", new_max);
        }
    }
}

// 获取全局内存上限
static void update_max_global_memory(void) {
    // 从环境变量读取全局内存上限
    const char *max_memory_str = getenv("RDMA_INTERCEPT_MAX_GLOBAL_MEMORY");
    if (max_memory_str) {
        uint64_t new_max = atoll(max_memory_str);
        if (new_max > 0) {
            pthread_mutex_lock(&memory_count_mutex);
            max_global_memory = new_max;
            pthread_mutex_unlock(&memory_count_mutex);
            printf("更新全局内存上限为: %llu bytes\n", (unsigned long long)new_max);
        }
    }
}

// 信号处理函数
static void signal_handler(int sig __attribute__((unused)))
{
    running = 0;
    printf("\n收到信号，正在退出...\n");
}

// 从eBPF映射中读取数据并同步到共享内存
static void sync_ebpf_data_to_shared_memory(void)
{
    pthread_mutex_lock(&ebpf_sync_mutex);
    
    shared_memory_data_t* shm_data = shm_get_ptr();
    if (!shm_data) {
        pthread_mutex_unlock(&ebpf_sync_mutex);
        return;
    }

    // 首先同步全局资源数据
    if (global_resources_map_fd >= 0) {
        uint32_t key = 0;
        resource_usage_t global_usage = {};
        
        int lookup_err = bpf_map_lookup_elem(global_resources_map_fd, &key, &global_usage);
        if (lookup_err == 0) {
            printf("[COLLECTOR] 从eBPF map读取全局资源: QP=%d, MR=%d, Memory=%llu\n", 
                   global_usage.qp_count, global_usage.mr_count, (unsigned long long)global_usage.memory_used);
            // 更新共享内存中的全局计数
            shm_lock(shm_data);
            shm_data->global_stats.qp_count = global_usage.qp_count;
            shm_data->global_stats.mr_count = global_usage.mr_count;
            shm_data->global_stats.memory_used = global_usage.memory_used;
            shm_data->version++;
            shm_unlock(shm_data);
            printf("[COLLECTOR] 已更新共享内存中的全局计数: QP=%d, MR=%d, Memory=%llu\n", 
                   shm_data->global_stats.qp_count, shm_data->global_stats.mr_count, (unsigned long long)shm_data->global_stats.memory_used);
        } else {
            printf("[COLLECTOR] 从eBPF map读取全局资源失败: %d\n", lookup_err);
        }
    }
    
    // 同步进程资源数据
    if (process_resources_map_fd >= 0) {
        // 读取eBPF map中的所有进程资源数据
        uint32_t current_pid = 0;
        uint32_t next_pid = 0;
        resource_usage_t usage = {};
        
        // 遍历进程资源映射
        while (bpf_map_get_next_key(process_resources_map_fd, &current_pid, &next_pid) == 0) {
            if (bpf_map_lookup_elem(process_resources_map_fd, &next_pid, &usage) == 0) {
                // 更新共享内存中的进程资源数据
                shm_update_process_resources(next_pid, &usage);
            }
            current_pid = next_pid;
        }
    }
    
    pthread_mutex_unlock(&ebpf_sync_mutex);
}

// eBPF数据同步线程函数
static void *sync_thread_func(void *arg __attribute__((unused)))
{
    // 定期从eBPF maps同步数据到共享内存
    struct timespec interval = {.tv_sec = 0, .tv_nsec = 100000000}; // 每100ms同步一次（100,000,000纳秒 = 100毫秒）
    
    while (running) {
        sync_ebpf_data_to_shared_memory();
        nanosleep(&interval, NULL);
    }
    
    return NULL;
}

// 初始化数据收集服务
static int initialize_service(void)
{
    // 初始化共享内存
    if (shm_init() != 0) {
        fprintf(stderr, "初始化共享内存失败\n");
        return -1;
    }
    
    // 从环境变量读取全局QP上限
    update_max_global_qp();
    
    // 从环境变量读取全局内存上限
    update_max_global_memory();
    
    // 设置全局资源限制
    shm_set_global_limits(max_global_qp, 1000, max_global_memory);
    
    // 获取eBPF映射文件描述符
    process_resources_map_fd = bpf_obj_get("/sys/fs/bpf/process_resources");
    if (process_resources_map_fd < 0) {
        fprintf(stderr, "获取进程资源eBPF映射文件描述符失败: %d (errno: %s)\n", errno, strerror(errno));
        fprintf(stderr, "提示: 请确保eBPF程序已加载并将maps挂载到bpffs\n");
    } else {
        printf("成功获取进程资源eBPF映射文件描述符: %d\n", process_resources_map_fd);
    }
    
    global_resources_map_fd = bpf_obj_get("/sys/fs/bpf/global_resources");
    if (global_resources_map_fd < 0) {
        fprintf(stderr, "获取全局资源eBPF映射文件描述符失败: %d (errno: %s)\n", errno, strerror(errno));
        fprintf(stderr, "提示: 请确保eBPF程序已加载并将maps挂载到bpffs\n");
    } else {
        printf("成功获取全局资源eBPF映射文件描述符: %d\n", global_resources_map_fd);
    }
    
    // 启动数据同步线程
    int ret = pthread_create(&sync_thread, NULL, sync_thread_func, NULL);
    if (ret != 0) {
        fprintf(stderr, "创建数据同步线程失败: %d\n", ret);
        return -1;
    }
    
    printf("数据收集服务初始化成功，共享内存已准备就绪\n");
    return 0;
}

// 清理资源
static void cleanup_service(void)
{
    running = 0;
    
    if (sync_thread) {
        pthread_join(sync_thread, NULL);
    }
    
    if (process_resources_map_fd >= 0) {
        close(process_resources_map_fd);
    }
    
    if (global_resources_map_fd >= 0) {
        close(global_resources_map_fd);
    }
    
    printf("数据收集服务已停止\n");
}

// 主函数
int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
    printf("基于共享内存的数据收集服务启动中...\n");
    printf("从环境变量读取全局QP上限: %s -> %d\n", 
           getenv("RDMA_INTERCEPT_MAX_GLOBAL_QP") ? : "NULL", 
           max_global_qp);
    
    if (getenv("RDMA_INTERCEPT_MAX_GLOBAL_MEMORY")) {
        printf("未找到RDMA_INTERCEPT_MAX_GLOBAL_MEMORY环境变量\n");
    }
    
    // 注册信号处理器
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化服务
    if (initialize_service() != 0) {
        fprintf(stderr, "服务初始化失败\n");
        return 1;
    }
    
    printf("基于共享内存的数据收集服务已启动，正在同步eBPF数据到共享内存...\n");
    printf("按Ctrl+C退出\n");
    
    // 主循环 - 持续运行直到收到信号
    while (running) {
        sleep(1);
    }
    
    // 清理资源
    cleanup_service();
    
    return 0;
}