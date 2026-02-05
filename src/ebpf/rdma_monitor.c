#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

// 资源使用情况结构
typedef struct {
    int qp_count;
    int mr_count;
    uint64_t memory_used;
} resource_usage_t;

// 全局变量
static int running = 1;
static struct bpf_object *obj = NULL;
static int process_resources_map_fd = -1;
static int global_resources_map_fd = -1;
static struct bpf_link *links[10];  // 存储kprobe链接
static int num_links = 0;

// 信号处理函数
static void signal_handler(int sig)
{
    running = 0;
    printf("\n收到信号 %d，正在退出...\n", sig);
}

// 附加kprobe
static int attach_kprobe(const char *func_name, const char *prog_name)
{
    struct bpf_program *prog;
    struct bpf_link *link;

    prog = bpf_object__find_program_by_name(obj, prog_name);
    if (!prog) {
        fprintf(stderr, "无法找到程序: %s\n", prog_name);
        return -1;
    }

    link = bpf_program__attach_kprobe(prog, false, func_name);  // false 表示 attach 到 entry point
    if (!link) {
        fprintf(stderr, "无法附加kprobe到函数: %s\n", func_name);
        return -1;
    }

    links[num_links++] = link;
    printf("成功附加kprobe到函数: %s (程序: %s)\n", func_name, prog_name);
    return 0;
}

// 加载eBPF程序
static int load_bpf_program(void)
{
    int err;
    char *bpf_paths[] = {
        "rdma_monitor.bpf.o",
        "../../../src/ebpf/rdma_monitor.bpf.o",
        "../../src/ebpf/rdma_monitor.bpf.o",
        "../src/ebpf/rdma_monitor.bpf.o",
        "src/ebpf/rdma_monitor.bpf.o",
        NULL
    };

    // 尝试在不同的目录中加载eBPF对象文件
    for (int i = 0; bpf_paths[i]; i++) {
        obj = bpf_object__open_file(bpf_paths[i], NULL);
        if (obj) {
            printf("成功加载eBPF程序: %s\n", bpf_paths[i]);
            break;
        }
    }

    if (!obj) {
        fprintf(stderr, "无法打开eBPF对象文件\n");
        return -1;
    }

    // 加载eBPF程序到内核
    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "无法加载eBPF程序: %d\n", err);
        goto cleanup;
    }

    // 获取map文件描述符
    process_resources_map_fd = bpf_object__find_map_fd_by_name(obj, "process_resources");
    if (process_resources_map_fd < 0) {
        fprintf(stderr, "无法找到process_resources map\n");
        goto cleanup;
    }

    global_resources_map_fd = bpf_object__find_map_fd_by_name(obj, "global_resources");
    if (global_resources_map_fd < 0) {
        fprintf(stderr, "无法找到global_resources map\n");
        goto cleanup;
    }

    // 附加kprobe到相应的函数
    if (attach_kprobe("ib_uverbs_create_qp", "kprobe_create_qp") < 0) {
        fprintf(stderr, "警告: 无法附加ib_uverbs_create_qp kprobe\n");
    }
    if (attach_kprobe("ib_uverbs_destroy_qp", "kprobe_destroy_qp") < 0) {
        fprintf(stderr, "警告: 无法附加ib_uverbs_destroy_qp kprobe\n");
    }
    if (attach_kprobe("ib_uverbs_reg_mr", "kprobe_reg_mr") < 0) {
        fprintf(stderr, "警告: 无法附加ib_uverbs_reg_mr kprobe\n");
    }
    if (attach_kprobe("ib_uverbs_dereg_mr", "kprobe_dereg_mr") < 0) {
        fprintf(stderr, "警告: 无法附加ib_uverbs_dereg_mr kprobe\n");
    }
    if (attach_kprobe("ib_create_qp_user", "kprobe_create_qp_kernel") < 0) {
        fprintf(stderr, "警告: 无法附加ib_create_qp_user kprobe\n");
    }
    if (attach_kprobe("ib_dereg_mr_user", "kprobe_dereg_mr_kernel") < 0) {
        fprintf(stderr, "警告: 无法附加ib_dereg_mr_user kprobe\n");
    }

    // 尝试挂载到bpffs（如果尚未挂载，需要先挂载bpffs）
    system("mkdir -p /sys/fs/bpf");
    
    // 获取map指针
    struct bpf_map *process_resources_map = bpf_object__find_map_by_name(obj, "process_resources");
    struct bpf_map *global_resources_map = bpf_object__find_map_by_name(obj, "global_resources");
    
    if (!process_resources_map) {
        fprintf(stderr, "无法获取process_resources map指针\n");
    } else {
        // 将maps固定到bpffs，以便collector_server可以访问
        err = bpf_map__pin(process_resources_map, "/sys/fs/bpf/process_resources");
        if (err) {
            fprintf(stderr, "无法将process_resources map固定到bpffs: %d (errno: %s)\n", err, strerror(errno));
            // 非致命错误，collector_server可以使用文件描述符直接访问
        } else {
            printf("process_resources map已固定到bpffs\n");
        }
    }
    
    if (!global_resources_map) {
        fprintf(stderr, "无法获取global_resources map指针\n");
    } else {
        err = bpf_map__pin(global_resources_map, "/sys/fs/bpf/global_resources");
        if (err) {
            fprintf(stderr, "无法将global_resources map固定到bpffs: %d (errno: %s)\n", err, strerror(errno));
            // 非致命错误，collector_server可以使用文件描述符直接访问
        } else {
            printf("global_resources map已固定到bpffs\n");
        }
    }

    return 0;

cleanup:
    if (obj) {
        bpf_object__close(obj);
        obj = NULL;
    }
    process_resources_map_fd = -1;
    global_resources_map_fd = -1;
    return err;
}

// 获取进程资源使用情况
static int get_process_resources(int pid, resource_usage_t *usage)
{
    if (process_resources_map_fd < 0 || !usage) {
        return -1;
    }

    int err = bpf_map_lookup_elem(process_resources_map_fd, &pid, usage);
    if (err) {
        // 如果进程不存在，返回空资源使用情况
        if (err == ENOENT) {
            memset(usage, 0, sizeof(resource_usage_t));
            return 0;
        }
        return err;
    }

    return 0;
}

// 获取全局资源使用情况
static int get_global_resources(resource_usage_t *usage)
{
    if (global_resources_map_fd < 0 || !usage) {
        return -1;
    }

    int key = 0;
    int err = bpf_map_lookup_elem(global_resources_map_fd, &key, usage);
    if (err) {
        // 如果全局资源不存在，返回空资源使用情况
        if (err == ENOENT) {
            memset(usage, 0, sizeof(resource_usage_t));
            return 0;
        }
        return err;
    }

    return 0;
}

// 打印资源使用情况
static void print_resources(void)
{
    resource_usage_t proc_usage, global_usage;
    int pid = getpid();

    // 获取当前进程资源使用情况
    if (get_process_resources(pid, &proc_usage) == 0) {
        printf("当前进程资源使用情况:\n");
        printf("QP数量: %d\n", proc_usage.qp_count);
        printf("MR数量: %d\n", proc_usage.mr_count);
        printf("内存使用: %llu bytes\n", (unsigned long long)proc_usage.memory_used);
    }

    // 获取全局资源使用情况
    if (get_global_resources(&global_usage) == 0) {
        printf("\n全局资源使用情况:\n");
        printf("QP数量: %d\n", global_usage.qp_count);
        printf("MR数量: %d\n", global_usage.mr_count);
        printf("内存使用: %llu bytes\n", (unsigned long long)global_usage.memory_used);
    }
}

// 启动事件监听
static int start_event_listener(void)
{
    printf("eBPF监控已启动，正在监听RDMA操作...\n");
    printf("按Ctrl+C退出\n\n");

    while (running) {
        sleep(1);
        // 每5秒打印一次资源使用情况
        static int count = 0;
        if (++count % 5 == 0) {
            print_resources();
            printf("\n");
        }
    }

    return 0;
}

// 主函数
int main(int argc, char *argv[])
{
    int err;

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 加载eBPF程序
    err = load_bpf_program();
    if (err) {
        fprintf(stderr, "加载eBPF程序失败\n");
        return 1;
    }

    // 检查命令行参数
    if (argc > 1) {
        if (strcmp(argv[1], "--get-resources") == 0) {
            // 获取并打印资源使用情况
            print_resources();
            goto cleanup;
        } else if (strcmp(argv[1], "--get-global") == 0) {
            // 只获取全局资源使用情况
            resource_usage_t global_usage;
            if (get_global_resources(&global_usage) == 0) {
                printf("%d %d %llu\n", global_usage.qp_count, global_usage.mr_count, 
                       (unsigned long long)global_usage.memory_used);
            }
            goto cleanup;
        } else if (strcmp(argv[1], "--get-process") == 0) {
            // 获取指定进程的资源使用情况
            int pid = getpid();
            if (argc > 2) {
                pid = atoi(argv[2]);
            }
            resource_usage_t proc_usage;
            if (get_process_resources(pid, &proc_usage) == 0) {
                printf("%d %d %llu\n", proc_usage.qp_count, proc_usage.mr_count, 
                       (unsigned long long)proc_usage.memory_used);
            }
            goto cleanup;
        }
    }

    // 启动事件监听
    err = start_event_listener();
    if (err) {
        fprintf(stderr, "启动事件监听失败\n");
        goto cleanup;
    }

    printf("eBPF监控已停止\n");

cleanup:
    if (obj) {
        bpf_object__close(obj);
    }
    return err;
}


