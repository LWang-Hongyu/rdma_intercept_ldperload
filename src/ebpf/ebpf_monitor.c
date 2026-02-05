#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdint.h>

// 资源使用情况结构
typedef struct {
    int qp_count;
    int mr_count;
    uint64_t memory_used;
} resource_usage_t;

// 全局变量 - 跟踪eBPF监控初始化状态
static int ebpf_monitor_initialized = 0;  // 0表示失败，1表示成功

// 通过collector_server获取进程资源使用情况
static int get_process_resources_via_collector(int pid, resource_usage_t *usage)
{
    if (!usage) {
        return -1;
    }
    
    char buffer[1024];
    int n;
    int temp_fd = -1;
    struct sockaddr_un addr;

    // 创建临时socket连接
    temp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (temp_fd < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 无法创建collector socket: %d\n", errno);
        return -1;
    }

    // 准备地址
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/rdma_collector.sock", sizeof(addr.sun_path) - 1);

    // 连接到服务
    int err = connect(temp_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 无法连接到数据收集服务: %d\n", errno);
        close(temp_fd);
        return -1;
    }

    // 发送请求获取特定进程的统计信息
    char request[256];
    snprintf(request, sizeof(request), "GET_PROC_STATS:%d", pid);
    n = write(temp_fd, request, strlen(request));
    if (n < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 无法发送请求: %d\n", errno);
        close(temp_fd);
        return -1;
    }

    // 读取响应
    n = read(temp_fd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 无法读取响应: %d\n", errno);
        close(temp_fd);
        return -1;
    }

    buffer[n] = '\0';

    // 解析响应 - 格式: "QP:1,MR:2,Memory:4096"
    char *qp_str = strstr(buffer, "QP:");
    char *mr_str = strstr(buffer, ",MR:");
    char *mem_str = strstr(buffer, ",Memory:");
    
    if (qp_str && mr_str && mem_str) {
        usage->qp_count = atoi(qp_str + 3);
        usage->mr_count = atoi(mr_str + 4);
        usage->memory_used = atol(mem_str + 8);
        
        fprintf(stderr, "[EBPF_MONITOR] 获取进程资源使用情况成功: PID=%d, QP=%d, MR=%d, Memory=%llu\n", 
                pid, usage->qp_count, usage->mr_count, (unsigned long long)usage->memory_used);
        
        close(temp_fd);
        return 0;
    } else {
        // 如果特定进程信息不可用，返回0值
        memset(usage, 0, sizeof(resource_usage_t));
        close(temp_fd);
        return 0;  // 返回0表示成功，但没有找到进程信息
    }
}

// 通过collector_server获取全局资源使用情况
static int get_global_resources_via_collector(resource_usage_t *usage)
{
    if (!usage) {
        return -1;
    }
    
    char buffer[1024];
    int n;
    int temp_fd = -1;
    struct sockaddr_un addr;

    // 创建临时socket连接
    temp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (temp_fd < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 无法创建collector socket: %d\n", errno);
        return -1;
    }

    // 准备地址
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/rdma_collector.sock", sizeof(addr.sun_path) - 1);

    // 连接到服务
    int err = connect(temp_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 无法连接到数据收集服务: %d\n", errno);
        close(temp_fd);
        return -1;
    }

    // 发送GET_STATS请求
    n = write(temp_fd, "GET_STATS", 9);
    if (n < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 无法发送请求: %d\n", errno);
        close(temp_fd);
        return -1;
    }

    // 读取响应
    n = read(temp_fd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 无法读取响应: %d\n", errno);
        close(temp_fd);
        return -1;
    }

    buffer[n] = '\0';

    // 解析响应
    char *total_qp_line = strstr(buffer, "Total QP:");
    char *total_mr_line = strstr(buffer, "Total MR:");
    char *memory_line = strstr(buffer, "Total Memory Used:");
    
    if (total_qp_line && total_mr_line && memory_line) {
        sscanf(total_qp_line, "Total QP: %d", &usage->qp_count);
        sscanf(total_mr_line, "Total MR: %d", &usage->mr_count);
        sscanf(memory_line, "Total Memory Used: %llu", (unsigned long long*)&usage->memory_used);
        
        fprintf(stderr, "[EBPF_MONITOR] 获取全局资源使用情况成功: QP=%d, MR=%d, Memory=%llu\n", 
                usage->qp_count, usage->mr_count, (unsigned long long)usage->memory_used);
        
        close(temp_fd);
        return 0;
    } else {
        // 如果解析失败，返回0值
        memset(usage, 0, sizeof(resource_usage_t));
        close(temp_fd);
        return 0;  // 返回0表示成功，但没有找到全局信息
    }
}

// 初始化eBPF监控 - 现在改为检查collector_server连接
int ebpf_monitor_init(void)
{
    fprintf(stderr, "[EBPF_MONITOR] 开始初始化eBPF监控（通过collector_server）\n");
    
    // 尝试连接到collector_server以确认其运行状态
    char buffer[256];
    int n;
    int temp_fd = -1;
    struct sockaddr_un addr;

    // 创建临时socket连接
    temp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (temp_fd < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 初始化失败: 无法创建socket: %d\n", errno);
        ebpf_monitor_initialized = 0;
        return -1;
    }

    // 准备地址
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/rdma_collector.sock", sizeof(addr.sun_path) - 1);

    // 连接到服务
    int err = connect(temp_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 初始化失败: 无法连接到collector_server: %d\n", errno);
        fprintf(stderr, "[EBPF_MONITOR] 提示: 请确保collector_server正在运行\n");
        close(temp_fd);
        ebpf_monitor_initialized = 0;
        return -1;
    }

    // 发送简单测试请求
    n = write(temp_fd, "GET_STATS", 9);
    if (n < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 初始化失败: 无法发送测试请求: %d\n", errno);
        close(temp_fd);
        ebpf_monitor_initialized = 0;
        return -1;
    }

    // 读取响应
    n = read(temp_fd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        fprintf(stderr, "[EBPF_MONITOR] 初始化失败: 无法读取测试响应: %d\n", errno);
        close(temp_fd);
        ebpf_monitor_initialized = 0;
        return -1;
    }

    buffer[n] = '\0';
    close(temp_fd);

    fprintf(stderr, "[EBPF_MONITOR] 初始化成功，已连接到collector_server\n");
    ebpf_monitor_initialized = 1;  // 标记初始化成功
    return 0;
}

// 关闭eBPF监控
void ebpf_monitor_cleanup(void)
{
    fprintf(stderr, "[EBPF_MONITOR] 关闭eBPF监控\n");
    ebpf_monitor_initialized = 0;
}

// 获取进程资源使用情况（供动态链接库使用）
int ebpf_get_process_resources(int pid, resource_usage_t *usage)
{
    // 检查eBPF监控是否已初始化
    if (!ebpf_monitor_initialized) {
        // 如果eBPF未初始化，返回错误，让调用方使用本地计数作为备用
        return -1;
    }
    
    if (!usage) {
        fprintf(stderr, "[EBPF_MONITOR] usage参数为NULL\n");
        return -1;
    }
    
    int err = get_process_resources_via_collector(pid, usage);
    if (err) {
        fprintf(stderr, "[EBPF_MONITOR] 通过collector获取进程资源使用情况失败: %d\n", err);
        return err;
    }
    
    return 0;
}

// 获取全局资源使用情况（供动态链接库使用）
int ebpf_get_global_resources(resource_usage_t *usage)
{
    // 检查eBPF监控是否已初始化
    if (!ebpf_monitor_initialized) {
        // 如果eBPF未初始化，返回错误，让调用方使用本地计数作为备用
        return -1;
    }
    
    int err = get_global_resources_via_collector(usage);
    if (err) {
        fprintf(stderr, "[EBPF_MONITOR] 通过collector获取全局资源使用情况失败: %d\n", err);
        return err;
    }
    return 0;
}