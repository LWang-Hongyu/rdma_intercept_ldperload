#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include "shared_memory.h"

// 全局共享内存文件描述符和指针
static int shm_fd = -1;
static shared_memory_data_t* shm_data_ptr = NULL;

// 获取当前时间戳（纳秒）
static uint64_t get_current_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int shm_init(void) {
    // 创建或打开共享内存对象
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[SHM] shm_open failed");
        return -1;
    }

    // 设置共享内存大小
    if (ftruncate(shm_fd, sizeof(shared_memory_data_t)) == -1) {
        perror("[SHM] ftruncate failed");
        close(shm_fd);
        shm_fd = -1;
        return -1;
    }

    // 映射共享内存到进程地址空间
    shm_data_ptr = (shared_memory_data_t*)mmap(NULL, sizeof(shared_memory_data_t),
                                              PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_data_ptr == MAP_FAILED) {
        perror("[SHM] mmap failed");
        close(shm_fd);
        shm_fd = -1;
        shm_data_ptr = NULL;
        return -1;
    }

    // 初始化共享内存数据（仅首次创建时）
    if (shm_data_ptr->global_stats.qp_count == 0 &&
        shm_data_ptr->global_stats.mr_count == 0 &&
        shm_data_ptr->global_stats.memory_used == 0) {
        // 初始化全局统计
        shm_data_ptr->global_stats.qp_count = 0;
        shm_data_ptr->global_stats.mr_count = 0;
        shm_data_ptr->global_stats.memory_used = 0;

        // 初始化进程统计数组
        for (int i = 0; i < MAX_PROCESSES; i++) {
            shm_data_ptr->process_stats[i].qp_count = 0;
            shm_data_ptr->process_stats[i].mr_count = 0;
            shm_data_ptr->process_stats[i].memory_used = 0;
            shm_data_ptr->process_pids[i] = 0;
        }

        // 初始化全局配置
        shm_data_ptr->max_global_qp = 1000;  // 默认值
        shm_data_ptr->max_global_mr = 1000;  // 默认值
        shm_data_ptr->max_global_memory = 1024UL * 1024UL * 1024UL;  // 1GB 默认值

        // 初始化同步机制
        shm_data_ptr->shm_lock = 0;

        // 初始化版本号和时间戳
        shm_data_ptr->version = 1;
        shm_data_ptr->last_update_time = get_current_time_ns();
    }

    fprintf(stderr, "[SHM] Shared memory initialized successfully\n");
    return 0;
}

int shm_destroy(void) {
    if (shm_data_ptr != NULL) {
        munmap(shm_data_ptr, sizeof(shared_memory_data_t));
        shm_data_ptr = NULL;
    }

    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }

    // 删除共享内存对象
    if (shm_unlink(SHM_NAME) == -1) {
        perror("[SHM] shm_unlink failed");
        return -1;
    }

    fprintf(stderr, "[SHM] Shared memory destroyed successfully\n");
    return 0;
}

shared_memory_data_t* shm_get_ptr(void) {
    return shm_data_ptr;
}

// 自旋锁实现
void shm_lock(shared_memory_data_t* data) {
    int expected;
    do {
        expected = 0;
    } while (!__atomic_compare_exchange_n(&data->shm_lock, &expected, 1, 
                                         false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
}

void shm_unlock(shared_memory_data_t* data) {
    __atomic_store_n(&data->shm_lock, 0, __ATOMIC_RELEASE);
}

int shm_get_global_resources(resource_usage_t* usage) {
    if (!usage || !shm_data_ptr) {
        return -1;
    }

    // 简单复制，不加锁以提高性能，可能存在轻微数据不一致（可接受）
    usage->qp_count = shm_data_ptr->global_stats.qp_count;
    usage->mr_count = shm_data_ptr->global_stats.mr_count;
    usage->memory_used = shm_data_ptr->global_stats.memory_used;

    return 0;
}

int shm_get_process_resources(pid_t pid, resource_usage_t* usage) {
    if (!usage || !shm_data_ptr || pid <= 0) {
        return -1;
    }

    // 查找对应进程的资源统计
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (shm_data_ptr->process_pids[i] == pid) {
            usage->qp_count = shm_data_ptr->process_stats[i].qp_count;
            usage->mr_count = shm_data_ptr->process_stats[i].mr_count;
            usage->memory_used = shm_data_ptr->process_stats[i].memory_used;
            return 0;
        }
    }

    // 如果没找到，返回0值
    usage->qp_count = 0;
    usage->mr_count = 0;
    usage->memory_used = 0;
    return 0; // 成功但未找到特定进程信息
}

int shm_update_global_resources(const resource_usage_t* usage) {
    if (!usage || !shm_data_ptr) {
        return -1;
    }

    shm_lock(shm_data_ptr);

    shm_data_ptr->global_stats.qp_count = usage->qp_count;
    shm_data_ptr->global_stats.mr_count = usage->mr_count;
    shm_data_ptr->global_stats.memory_used = usage->memory_used;

    // 更新版本号和时间戳
    shm_data_ptr->version++;
    shm_data_ptr->last_update_time = get_current_time_ns();

    shm_unlock(shm_data_ptr);

    return 0;
}

int shm_update_process_resources(pid_t pid, const resource_usage_t* usage) {
    if (!usage || !shm_data_ptr || pid <= 0) {
        return -1;
    }

    shm_lock(shm_data_ptr);

    // 查找现有条目或寻找空槽
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (shm_data_ptr->process_pids[i] == pid) {
            slot = i;
            break;
        } else if (shm_data_ptr->process_pids[i] == 0 && slot == -1) {
            slot = i;  // 找到第一个空槽
        }
    }

    // 如果没有找到现有条目也没有空槽，则失败
    if (slot == -1) {
        shm_unlock(shm_data_ptr);
        return -1;
    }

    // 更新或创建进程资源统计
    if (shm_data_ptr->process_pids[slot] == 0) {
        // 新增进程
        shm_data_ptr->process_pids[slot] = pid;
    }

    shm_data_ptr->process_stats[slot].qp_count = usage->qp_count;
    shm_data_ptr->process_stats[slot].mr_count = usage->mr_count;
    shm_data_ptr->process_stats[slot].memory_used = usage->memory_used;

    // 更新版本号和时间戳
    shm_data_ptr->version++;
    shm_data_ptr->last_update_time = get_current_time_ns();

    shm_unlock(shm_data_ptr);

    return 0;
}

int shm_set_global_limits(uint32_t max_qp, uint32_t max_mr, uint64_t max_memory) {
    if (!shm_data_ptr) {
        return -1;
    }

    shm_lock(shm_data_ptr);

    shm_data_ptr->max_global_qp = max_qp;
    shm_data_ptr->max_global_mr = max_mr;
    shm_data_ptr->max_global_memory = max_memory;

    shm_unlock(shm_data_ptr);

    return 0;
}