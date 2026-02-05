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

// 定义请求类型
#define REQUEST_GET_STATS 1
#define REQUEST_QP_CREATE 2
#define REQUEST_QP_DESTROY 3

// 资源使用情况结构（与eBPF程序保持一致）
typedef struct {
    int qp_count;
    int mr_count;
    uint64_t memory_used;
} resource_usage_t;

// 进程资源使用结构
typedef struct {
    uint32_t qp_count;
    uint32_t mr_count;
    uint64_t memory_used;
} process_resource_usage_t;

// 进程资源哈希表节点
typedef struct process_node {
    uint32_t pid;
    process_resource_usage_t usage;
    struct process_node *next;
} process_node_t;

// 全局变量
static volatile int running = 1;
static int server_fd = -1;
static uint32_t global_qp_count = 0;
static uint32_t max_global_qp = 10; // 默认全局QP上限
static pthread_mutex_t qp_count_mutex = PTHREAD_MUTEX_INITIALIZER;

// 内存资源全局变量
static uint32_t global_mr_count = 0;
static uint64_t global_memory_used = 0;
static uint64_t max_global_memory = 1024ULL * 1024ULL * 1024ULL * 10; // 默认全局内存上限10GB
static pthread_mutex_t memory_count_mutex = PTHREAD_MUTEX_INITIALIZER;

// 进程资源哈希表
static process_node_t *process_resources[1024]; // 简单哈希表，大小为1024
static pthread_mutex_t process_resources_mutex = PTHREAD_MUTEX_INITIALIZER;

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

// 从eBPF映射中读取数据并同步到本地哈希表
static void sync_ebpf_data(void)
{
    pthread_mutex_lock(&ebpf_sync_mutex);
    
    // 首先同步进程资源数据
    if (process_resources_map_fd >= 0) {
        // 读取eBPF map中的所有进程资源数据
        uint32_t current_pid = 0;
        uint32_t next_pid = 0;
        resource_usage_t usage = {};
        
        // 遍历进程资源映射
        while (bpf_map_get_next_key(process_resources_map_fd, &current_pid, &next_pid) == 0) {
            if (bpf_map_lookup_elem(process_resources_map_fd, &next_pid, &usage) == 0) {
                // 更新本地哈希表中的进程资源数据
                pthread_mutex_lock(&process_resources_mutex);
                
                int hash_index = next_pid % 1024;
                process_node_t *node = process_resources[hash_index];
                
                // 查找现有节点
                while (node != NULL) {
                    if (node->pid == next_pid) {
                        // 更新现有节点
                        node->usage.qp_count = usage.qp_count;
                        node->usage.mr_count = usage.mr_count;
                        node->usage.memory_used = usage.memory_used;
                        break;
                    }
                    node = node->next;
                }
                
                // 如果没找到，创建新节点
                if (node == NULL) {
                    node = malloc(sizeof(process_node_t));
                    if (node != NULL) {
                        node->pid = next_pid;
                        node->usage.qp_count = usage.qp_count;
                        node->usage.mr_count = usage.mr_count;
                        node->usage.memory_used = usage.memory_used;
                        node->next = process_resources[hash_index];
                        process_resources[hash_index] = node;
                    }
                }
                
                pthread_mutex_unlock(&process_resources_mutex);
            }
            current_pid = next_pid;
        }
    }
    
    // 同步全局资源数据
    if (global_resources_map_fd >= 0) {
        uint32_t key = 0;
        resource_usage_t global_usage = {};
        
        if (bpf_map_lookup_elem(global_resources_map_fd, &key, &global_usage) == 0) {
            // 更新全局计数
            pthread_mutex_lock(&qp_count_mutex);
            global_qp_count = global_usage.qp_count;
            pthread_mutex_unlock(&qp_count_mutex);
            
            pthread_mutex_lock(&memory_count_mutex);
            global_mr_count = global_usage.mr_count;
            global_memory_used = global_usage.memory_used;
            pthread_mutex_unlock(&memory_count_mutex);
        }
    }
    
    pthread_mutex_unlock(&ebpf_sync_mutex);
}

// eBPF数据同步线程函数
static void *sync_thread_func(void *arg __attribute__((unused)))
{
    // 定期从eBPF maps同步数据
    struct timespec interval = {.tv_sec = 1, .tv_nsec = 0}; // 每秒同步一次
    
    while (running) {
        sync_ebpf_data();
        nanosleep(&interval, NULL);
    }
    
    return NULL;
}




// 初始化数据收集服务
static int initialize_service(void)
{
    // 初始化全局QP计数为0
    global_qp_count = 0;
    
    // 初始化全局内存计数为0
    global_mr_count = 0;
    global_memory_used = 0;
    
    // 初始化进程资源哈希表
    memset(process_resources, 0, sizeof(process_resources));
    
    // 从环境变量读取全局QP上限
    update_max_global_qp();
    
    // 从环境变量读取全局内存上限
    update_max_global_memory();
    
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
    
    printf("数据收集服务初始化成功，全局QP计数重置为0，全局内存计数重置为0\n");
    return 0;
}

// 获取进程资源使用情况
static int get_process_resource_usage(uint32_t pid, process_resource_usage_t *usage)
{
    pthread_mutex_lock(&process_resources_mutex);
    
    int hash_index = pid % 1024;
    process_node_t *node = process_resources[hash_index];
    
    while (node != NULL) {
        if (node->pid == pid) {
            *usage = node->usage;
            pthread_mutex_unlock(&process_resources_mutex);
            return 0;
        }
        node = node->next;
    }
    
    // 如果没找到，返回0值
    memset(usage, 0, sizeof(process_resource_usage_t));
    pthread_mutex_unlock(&process_resources_mutex);
    return 0;
}

// 更新进程资源使用情况


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

                // 检查是否为GET_PROC_STATS请求
                if (strncmp(buffer, "GET_PROC_STATS:", 15) == 0) {
                    // 在获取进程资源前，先同步eBPF数据以确保是最新的
                    sync_ebpf_data();
                    
                    // 解析PID
                    uint32_t pid = (uint32_t)atoi(buffer + 15);
                    
                    // 获取进程资源使用情况
                    process_resource_usage_t proc_usage;
                    int result = get_process_resource_usage(pid, &proc_usage);
                    
                    char response[256];
                    if (result == 0) {
                        snprintf(response, sizeof(response), "QP:%u,MR:%u,Memory:%llu",
                                proc_usage.qp_count, proc_usage.mr_count, 
                                (unsigned long long)proc_usage.memory_used);
                    } else {
                        snprintf(response, sizeof(response), "QP:0,MR:0,Memory:0");
                    }
                    
                    // 发送响应
                    n = write(client_fd, response, strlen(response));
                    if (n < 0) {
                        fprintf(stderr, "发送响应失败: %d\n", errno);
                    }
                }
                // 处理GET_STATS请求
                else if (strcmp(buffer, "GET_STATS") == 0) {
                    // 在返回统计数据前，先同步eBPF数据以确保是最新的
                    sync_ebpf_data();
                    
                    // 构建响应
                    char response[512];
                    pthread_mutex_lock(&qp_count_mutex);
                    uint32_t current_qp_count = global_qp_count;
                    uint32_t current_max_qp = max_global_qp;
                    pthread_mutex_unlock(&qp_count_mutex);
                    
                    pthread_mutex_lock(&memory_count_mutex);
                    uint32_t current_mr_count = global_mr_count;
                    uint64_t current_memory_used = global_memory_used;
                    uint64_t current_max_memory = max_global_memory;
                    pthread_mutex_unlock(&memory_count_mutex);
                    
                    snprintf(response, sizeof(response), "Total QP: %u\nMax QP: %u\n" 
                             "Total MR: %u\nTotal Memory Used: %llu bytes\nMax Memory: %llu bytes\n", 
                             current_qp_count, current_max_qp,
                             current_mr_count, (unsigned long long)current_memory_used, 
                             (unsigned long long)current_max_memory);

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
                } else if (strncmp(buffer, "MR_CREATE ", 10) == 0) {
                    // 处理MR创建事件
                    size_t length = 0;
                    if (sscanf(buffer + 10, "%zu", &length) == 1) {
                        pthread_mutex_lock(&memory_count_mutex);
                        if (global_memory_used + length > max_global_memory) {
                            // 达到内存上限，拒绝创建
                            pthread_mutex_unlock(&memory_count_mutex);
                            const char *error_response = "Error: Memory limit reached\n";
                            n = write(client_fd, error_response, strlen(error_response));
                            if (n < 0) {
                                fprintf(stderr, "发送响应失败: %d\n", errno);
                            }
                        } else {
                            // 未达到内存上限，允许创建
                            global_mr_count++;
                            global_memory_used += length;
                            pthread_mutex_unlock(&memory_count_mutex);
                            
                            // 发送成功响应
                            const char *success_response = "Success: MR created\n";
                            n = write(client_fd, success_response, strlen(success_response));
                            if (n < 0) {
                                fprintf(stderr, "发送响应失败: %d\n", errno);
                            }
                        }
                    } else {
                        // 解析失败
                        const char *error_response = "Error: Invalid MR_CREATE request\n";
                        n = write(client_fd, error_response, strlen(error_response));
                        if (n < 0) {
                            fprintf(stderr, "发送响应失败: %d\n", errno);
                        }
                    }
                } else if (strncmp(buffer, "CHECK_MEMORY ", 13) == 0) {
                    // 处理内存检查事件
                    size_t length = 0;
                    if (sscanf(buffer + 13, "%zu", &length) == 1) {
                        pthread_mutex_lock(&memory_count_mutex);
                        if (global_memory_used + length > max_global_memory) {
                            // 达到内存上限，拒绝创建
                            pthread_mutex_unlock(&memory_count_mutex);
                            const char *error_response = "Error: Memory limit reached\n";
                            n = write(client_fd, error_response, strlen(error_response));
                            if (n < 0) {
                                fprintf(stderr, "发送响应失败: %d\n", errno);
                            }
                        } else {
                            // 未达到内存上限，允许创建
                            pthread_mutex_unlock(&memory_count_mutex);
                            
                            // 发送成功响应
                            const char *success_response = "Success: Memory check passed\n";
                            n = write(client_fd, success_response, strlen(success_response));
                            if (n < 0) {
                                fprintf(stderr, "发送响应失败: %d\n", errno);
                            }
                        }
                    } else {
                        // 解析失败
                        const char *error_response = "Error: Invalid CHECK_MEMORY request\n";
                        n = write(client_fd, error_response, strlen(error_response));
                        if (n < 0) {
                            fprintf(stderr, "发送响应失败: %d\n", errno);
                        }
                    }
                } else if (strncmp(buffer, "MR_DESTROY ", 11) == 0) {
                    // 处理MR销毁事件
                    size_t length = 0;
                    if (sscanf(buffer + 11, "%zu", &length) == 1) {
                        pthread_mutex_lock(&memory_count_mutex);
                        if (global_mr_count > 0) {
                            global_mr_count--;
                        }
                        if (length > 0 && global_memory_used >= length) {
                            global_memory_used -= length;
                        }
                        pthread_mutex_unlock(&memory_count_mutex);
                        
                        // 发送成功响应
                        const char *success_response = "Success: MR destroyed\n";
                        n = write(client_fd, success_response, strlen(success_response));
                        if (n < 0) {
                            fprintf(stderr, "发送响应失败: %d\n", errno);
                        }
                    } else {
                        // 解析失败
                        const char *error_response = "Error: Invalid MR_DESTROY request\n";
                        n = write(client_fd, error_response, strlen(error_response));
                        if (n < 0) {
                            fprintf(stderr, "发送响应失败: %d\n", errno);
                        }
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
    pthread_mutex_destroy(&memory_count_mutex);
    pthread_mutex_destroy(&process_resources_mutex);
    pthread_mutex_destroy(&ebpf_sync_mutex);
    
    // 停止并清理同步线程
    running = 0;
    if (pthread_join(sync_thread, NULL) != 0) {
        fprintf(stderr, "等待同步线程结束失败\n");
    }
    
    // 关闭eBPF映射文件描述符
    if (process_resources_map_fd >= 0) {
        close(process_resources_map_fd);
    }
    if (global_resources_map_fd >= 0) {
        close(global_resources_map_fd);
    }
}

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
    int err;

    // 在变成守护进程之前，先读取环境变量中的全局QP上限
    const char *max_qp_str = getenv("RDMA_INTERCEPT_MAX_GLOBAL_QP");
    uint32_t env_max_global_qp = 0;
    if (max_qp_str) {
        env_max_global_qp = atoi(max_qp_str);
        printf("从环境变量读取全局QP上限: %s -> %u\n", max_qp_str, env_max_global_qp);
    } else {
        printf("未找到RDMA_INTERCEPT_MAX_GLOBAL_QP环境变量\n");
    }

    // 在变成守护进程之前，先读取环境变量中的全局内存上限
    const char *max_memory_str = getenv("RDMA_INTERCEPT_MAX_GLOBAL_MEMORY");
    uint64_t env_max_global_memory = 0;
    if (max_memory_str) {
        env_max_global_memory = atoll(max_memory_str);
        printf("从环境变量读取全局内存上限: %s -> %llu bytes\n", max_memory_str, (unsigned long long)env_max_global_memory);
    } else {
        printf("未找到RDMA_INTERCEPT_MAX_GLOBAL_MEMORY环境变量\n");
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
    
    // 使用之前读取的环境变量值更新全局QP上限
    if (env_max_global_qp > 0) {
        pthread_mutex_lock(&qp_count_mutex);
        max_global_qp = env_max_global_qp;
        pthread_mutex_unlock(&qp_count_mutex);
        printf("从环境变量更新全局QP上限为: %u\n", env_max_global_qp);
    }

    // 使用之前读取的环境变量值更新全局内存上限
    if (env_max_global_memory > 0) {
        pthread_mutex_lock(&memory_count_mutex);
        max_global_memory = env_max_global_memory;
        pthread_mutex_unlock(&memory_count_mutex);
        printf("从环境变量更新全局内存上限为: %llu bytes\n", (unsigned long long)env_max_global_memory);
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
