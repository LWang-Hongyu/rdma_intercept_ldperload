#ifndef EBPF_MONITOR_H
#define EBPF_MONITOR_H

#include <stdint.h>

// 资源使用情况结构
typedef struct {
    int qp_count;
    int mr_count;
    uint64_t memory_used;
} resource_usage_t;

// 初始化eBPF监控
int ebpf_monitor_init(void);

// 关闭eBPF监控
void ebpf_monitor_cleanup(void);

// 获取进程资源使用情况
int ebpf_get_process_resources(int pid, resource_usage_t *usage);

// 获取全局资源使用情况
int ebpf_get_global_resources(resource_usage_t *usage);


#endif // EBPF_MONITOR_H
