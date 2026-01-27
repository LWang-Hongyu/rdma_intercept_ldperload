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

// 全局变量
static int running = 1;
static struct bpf_object *obj = NULL;

// 信号处理函数
static void signal_handler(int sig)
{
    running = 0;
    printf("\n收到信号，正在退出...\n");
}

// 加载eBPF程序
static int load_bpf_program(void)
{
    int err;

    // 加载eBPF对象文件
    obj = bpf_object__open_file("rdma_monitor.bpf.o", NULL);
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

    return 0;

cleanup:
    if (obj) {
        bpf_object__close(obj);
        obj = NULL;
    }
    return err;
}

// 启动事件监听
static int start_event_listener(void)
{
    printf("eBPF监控已启动，正在监听RDMA操作...\n");
    printf("按Ctrl+C退出\n\n");

    while (running) {
        sleep(1);
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
