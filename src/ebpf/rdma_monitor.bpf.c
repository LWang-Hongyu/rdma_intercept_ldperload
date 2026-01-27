// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

// 定义BPF map
struct bpf_map_def SEC("maps") qp_counts = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(int),
    .value_size = sizeof(int),
    .max_entries = 1024,
};

// 跟踪ibv_create_qp函数调用
SEC("kprobe/ibv_create_qp")
int kprobe_ibv_create_qp(struct pt_regs *ctx)
{
    int pid = bpf_get_current_pid_tgid() >> 32;
    int *count = bpf_map_lookup_elem(&qp_counts, &pid);
    if (count) {
        (*count)++;
    } else {
        int initial = 1;
        bpf_map_update_elem(&qp_counts, &pid, &initial, BPF_ANY);
    }
    return 0;
}

char _license[] SEC("license") = "GPL";
