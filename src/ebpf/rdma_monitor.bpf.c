// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <linux/stddef.h>
#include <linux/types.h>

// 定义资源使用情况结构
typedef struct {
    int qp_count;
    int mr_count;
    __u64 memory_used;
} resource_usage_t;

// 存储每个进程的资源使用情况
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);  // PID
    __type(value, resource_usage_t);
} process_resources SEC(".maps");

// 存储全局资源使用情况
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, resource_usage_t);
} global_resources SEC(".maps");

// 辅助函数：获取或初始化进程资源
static resource_usage_t *get_or_init_process_resources(__u32 pid) {
    resource_usage_t *usage = bpf_map_lookup_elem(&process_resources, &pid);
    if (!usage) {
        resource_usage_t initial_value = {0};
        bpf_map_update_elem(&process_resources, &pid, &initial_value, BPF_ANY);
        usage = bpf_map_lookup_elem(&process_resources, &pid);
    }
    return usage;
}

// 辅助函数：获取或初始化全局资源
static resource_usage_t *get_or_init_global_resources(void) {
    __u32 key = 0;
    resource_usage_t *usage = bpf_map_lookup_elem(&global_resources, &key);
    if (!usage) {
        resource_usage_t initial_value = {0};
        bpf_map_update_elem(&global_resources, &key, &initial_value, BPF_ANY);
        usage = bpf_map_lookup_elem(&global_resources, &key);
    }
    return usage;
}

// Kprobe: 跟踪用户态创建QP的入口点
SEC("kprobe/ib_uverbs_create_qp")
int kprobe_create_qp(struct pt_regs *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // 更新进程级QP计数
    resource_usage_t *proc_usage = get_or_init_process_resources(pid);
    if (proc_usage) {
        proc_usage->qp_count++;
    }
    
    // 更新全局QP计数
    resource_usage_t *global_usage = get_or_init_global_resources();
    if (global_usage) {
        global_usage->qp_count++;
    }
    
    return 0;
}

// Kprobe: 跟踪用户态QP销毁的入口点
SEC("kprobe/ib_uverbs_destroy_qp")
int kprobe_destroy_qp(struct pt_regs *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // 更新进程级QP计数
    resource_usage_t *proc_usage = get_or_init_process_resources(pid);
    if (proc_usage && proc_usage->qp_count > 0) {
        proc_usage->qp_count--;
    }
    
    // 更新全局QP计数
    resource_usage_t *global_usage = get_or_init_global_resources();
    if (global_usage && global_usage->qp_count > 0) {
        global_usage->qp_count--;
    }
    
    return 0;
}

// Kprobe: 跟踪用户态注册MR的入口点
SEC("kprobe/ib_uverbs_reg_mr")
int kprobe_reg_mr(struct pt_regs *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // 更新进程级资源使用情况
    resource_usage_t *proc_usage = get_or_init_process_resources(pid);
    if (proc_usage) {
        proc_usage->mr_count++;
    }
    
    // 更新全局资源使用情况
    resource_usage_t *global_usage = get_or_init_global_resources();
    if (global_usage) {
        global_usage->mr_count++;
        // 注意：这里无法获取实际内存大小，需要在用户态跟踪
    }
    
    return 0;
}

// Kprobe: 跟踪用户态注销MR的入口点
SEC("kprobe/ib_uverbs_dereg_mr")
int kprobe_dereg_mr(struct pt_regs *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // 更新进程级资源使用情况
    resource_usage_t *proc_usage = get_or_init_process_resources(pid);
    if (proc_usage && proc_usage->mr_count > 0) {
        proc_usage->mr_count--;
    }
    
    // 更新全局资源使用情况
    resource_usage_t *global_usage = get_or_init_global_resources();
    if (global_usage && global_usage->mr_count > 0) {
        global_usage->mr_count--;
    }
    
    return 0;
}

// Kprobe: 跟踪内核态创建QP的入口点
SEC("kprobe/ib_create_qp_user")
int kprobe_create_qp_kernel(struct pt_regs *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // 更新进程级QP计数
    resource_usage_t *proc_usage = get_or_init_process_resources(pid);
    if (proc_usage) {
        proc_usage->qp_count++;
    }
    
    // 更新全局QP计数
    resource_usage_t *global_usage = get_or_init_global_resources();
    if (global_usage) {
        global_usage->qp_count++;
    }
    
    return 0;
}

// Kprobe: 跟踪内核态注册MR的入口点
SEC("kprobe/ib_dereg_mr_user")
int kprobe_dereg_mr_kernel(struct pt_regs *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // 更新进程级资源使用情况
    resource_usage_t *proc_usage = get_or_init_process_resources(pid);
    if (proc_usage && proc_usage->mr_count > 0) {
        proc_usage->mr_count--;
    }
    
    // 更新全局资源使用情况
    resource_usage_t *global_usage = get_or_init_global_resources();
    if (global_usage && global_usage->mr_count > 0) {
        global_usage->mr_count--;
    }
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
