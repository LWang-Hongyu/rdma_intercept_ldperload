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

// 定义bpf_create_map_attr结构体
struct bpf_create_map_attr {
    __u32 map_type;
    __u32 key_size;
    __u32 value_size;
    __u32 max_entries;
    __u32 map_flags;
};

// 定义请求类型
#define REQUEST_GET_STATS 1
#define REQUEST_QP_CREATE 2
#define REQUEST_QP_DESTROY 3

// 全局变量
static volatile int running = 1;
static int server_fd = -1;
static uint32_t global_qp_count = 0;
static uint32_t max_global_qp = 10; // 默认全局QP上限
static pthread_mutex_t qp_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static int qp_counts_map_fd = -1; // eBPF映射文件描述符

// 信号处理函数
static void signal_handler(int sig __attribute__((unused)))
{
    running = 0;
    printf("\n收到信号，正在退出...\n");
}

// 获取eBPF映射文件描述符
static int get_bpf_map_fd(const char *map_path)
{
    return open(map_path, O_RDONLY);
}

// 创建eBPF映射
static int create_bpf_map(void)
{
    struct bpf_create_map_attr attr = {
        .map_type = BPF_MAP_TYPE_HASH,
        .key_size = sizeof(uint32_t),
        .value_size = sizeof(uint32_t),
        .max_entries = 1024,
        .map_flags = 0,
    };

    int fd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
    if (fd < 0) {
        fprintf(stderr, "创建eBPF映射失败: %s\n", strerror(errno));
        return -1;
    }

    return fd;
}

// 计算全局QP计数
static void calculate_global_qp_count(void)
{
    if (qp_counts_map_fd < 0) {
        // 如果eBPF映射文件描述符无效，使用当前的全局QP计数
        return;
    }

    // 从eBPF映射中读取真实的QP计数
    uint32_t total_qp_count = 0;
    uint32_t key = 0;
    uint32_t next_key = 0;
    int err = 0;

    // 遍历eBPF哈希表
    while (1) {
        // 获取下一个键
        err = syscall(__NR_bpf, BPF_MAP_GET_NEXT_KEY, qp_counts_map_fd, &key, &next_key);
        if (err < 0) {
            if (errno == ENOENT) {
                // 遍历结束
                break;
            }
            // 其他错误，退出遍历
            fprintf(stderr, "bpf_map_get_next_key失败: %d\n", errno);
            break;
        }

        // 查找当前键对应的值
        uint32_t value = 0;
        err = syscall(__NR_bpf, BPF_MAP_LOOKUP_ELEM, qp_counts_map_fd, &next_key, &value);
        if (err < 0) {
            fprintf(stderr, "bpf_map_lookup_elem失败: %d\n", errno);
        } else {
            // 累加QP计数
            total_qp_count += value;
        }

        // 更新键为下一个键
        key = next_key;
    }

    // 更新全局QP计数
    if (total_qp_count > 0) {
        pthread_mutex_lock(&qp_count_mutex);
        global_qp_count = total_qp_count;
        pthread_mutex_unlock(&qp_count_mutex);
        printf("从eBPF映射中读取QP计数: %u\n", total_qp_count);
    }
}


// 初始化数据收集服务
static int initialize_service(void)
{
    // 初始化全局QP计数为0
    global_qp_count = 0;
    
    // 获取eBPF映射文件描述符
    qp_counts_map_fd = get_bpf_map_fd("/sys/fs/bpf/qp_counts");
    if (qp_counts_map_fd < 0) {
        fprintf(stderr, "获取eBPF映射文件描述符失败: %d\n", errno);
        // 创建一个新的eBPF映射
        qp_counts_map_fd = create_bpf_map();
        if (qp_counts_map_fd < 0) {
            fprintf(stderr, "创建eBPF映射失败，将使用模拟的QP计数\n");
        } else {
            printf("创建eBPF映射成功，文件描述符: %d\n", qp_counts_map_fd);
        }
    }
    
    printf("数据收集服务初始化成功，全局QP计数重置为0\n");
    return 0;
}



// 启动Unix socket服务器
static int start_server(void)
{
    struct sockaddr_un addr;
    int err;

    // 创建socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        fprintf(stderr, "无法创建socket: %d\n", errno);
        return -1;
    }

    // 准备地址
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/rdma_collector.sock", sizeof(addr.sun_path) - 1);

    // 删除之前的socket文件
    unlink(addr.sun_path);

    // 绑定地址
    err = bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        fprintf(stderr, "无法绑定地址: %d\n", errno);
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    // 设置权限
    chmod(addr.sun_path, 0666);

    // 开始监听
    err = listen(server_fd, 10);
    if (err < 0) {
        fprintf(stderr, "无法开始监听: %d\n", errno);
        close(server_fd);
        unlink(addr.sun_path);
        server_fd = -1;
        return -1;
    }

    printf("数据收集服务已启动，监听: %s\n", addr.sun_path);
    return 0;
}

// 主循环
static void main_loop(void)
{
    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        // 计算最大文件描述符
        int max_fd = server_fd;

        // 等待文件描述符就绪
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno != EINTR) {
                fprintf(stderr, "select失败: %d\n", errno);
            }
            continue;
        }

        // 检查服务器文件描述符是否就绪
        if (FD_ISSET(server_fd, &read_fds)) {
            struct sockaddr_un client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            int client_fd;

            // 接受客户端连接
            client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
            if (client_fd < 0) {
                if (errno != EINTR) {
                    fprintf(stderr, "接受客户端连接失败: %d\n", errno);
                }
                continue;
            }

            // 直接处理客户端连接，不使用线程
            char buffer[1024];
            int n;

            // 读取客户端请求
            n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                // 移除字符串末尾的换行符
                buffer[n] = '\0';
                char *newline = strchr(buffer, '\n');
                if (newline) {
                    *newline = '\0';
                }

                // 处理GET_STATS请求
                if (strcmp(buffer, "GET_STATS") == 0) {
                    // 计算最新的全局QP计数
                    calculate_global_qp_count();

                    // 构建响应
                    char response[256];
                    pthread_mutex_lock(&qp_count_mutex);
                    uint32_t current_count = global_qp_count;
                    uint32_t current_max = max_global_qp;
                    pthread_mutex_unlock(&qp_count_mutex);
                    
                    snprintf(response, sizeof(response), "Total QP: %u\nMax QP: %u\n", 
                             current_count, current_max);

                    // 发送响应
                    n = write(client_fd, response, strlen(response));
                    if (n < 0) {
                        fprintf(stderr, "发送响应失败: %d\n", errno);
                    }
                } else if (strcmp(buffer, "QP_CREATE") == 0) {
                    // 处理QP创建事件
                    pthread_mutex_lock(&qp_count_mutex);
                    if (global_qp_count >= max_global_qp) {
                        // 达到QP上限，拒绝创建
                        pthread_mutex_unlock(&qp_count_mutex);
                        const char *error_response = "Error: QP limit reached\n";
                        n = write(client_fd, error_response, strlen(error_response));
                        if (n < 0) {
                            fprintf(stderr, "发送响应失败: %d\n", errno);
                        }
                    } else {
                        // 未达到QP上限，允许创建
                        global_qp_count++;
                        pthread_mutex_unlock(&qp_count_mutex);
                        
                        // 发送成功响应
                        const char *success_response = "Success: QP created\n";
                        n = write(client_fd, success_response, strlen(success_response));
                        if (n < 0) {
                            fprintf(stderr, "发送响应失败: %d\n", errno);
                        }
                    }
                } else if (strcmp(buffer, "QP_DESTROY") == 0) {
                    // 处理QP销毁事件
                    pthread_mutex_lock(&qp_count_mutex);
                    if (global_qp_count > 0) {
                        global_qp_count--;
                    }
                    pthread_mutex_unlock(&qp_count_mutex);
                    
                    // 发送成功响应
                    const char *success_response = "Success: QP destroyed\n";
                    n = write(client_fd, success_response, strlen(success_response));
                    if (n < 0) {
                        fprintf(stderr, "发送响应失败: %d\n", errno);
                    }
                } else {
                    // 处理未知请求
                    const char *error_response = "Error: Unknown request\n";
                    n = write(client_fd, error_response, strlen(error_response));
                    if (n < 0) {
                        fprintf(stderr, "发送响应失败: %d\n", errno);
                    }
                }
            } else if (n < 0) {
                fprintf(stderr, "读取客户端请求失败: %d\n", errno);
            }

            // 关闭连接
            if (close(client_fd) < 0) {
                fprintf(stderr, "关闭客户端连接失败: %d\n", errno);
            }
        }
    }
}

// 清理函数
static void cleanup(void)
{
    if (server_fd >= 0) {
        close(server_fd);
        unlink("/tmp/rdma_collector.sock");
    }

    pthread_mutex_destroy(&qp_count_mutex);
}

// 主函数
int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
    int err;

    // 将进程变成守护进程
    if (daemon(0, 0) < 0) {
        fprintf(stderr, "无法创建守护进程: %d\n", errno);
        return 1;
    }

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化数据收集服务
    err = initialize_service();
    if (err) {
        fprintf(stderr, "初始化数据收集服务失败\n");
        return 1;
    }

    // 启动服务器
    err = start_server();
    if (err) {
        fprintf(stderr, "启动服务器失败\n");
        cleanup();
        return 1;
    }

    // 运行主循环
    main_loop();

    // 清理
    cleanup();

    fprintf(stderr, "数据收集服务已退出\n");
    return 0;
}
