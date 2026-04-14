#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include "shm/shared_memory_tenant.h"
#include "dynamic_policy.h"

// 信号处理
static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// 打印使用帮助
static void print_usage(const char *program) {
    printf("Usage: %s [options]\n", program);
    printf("\nOptions:\n");
    printf("  -c, --create <id>        Create a new tenant with the specified ID\n");
    printf("  -n, --name <name>        Tenant name (use with --create)\n");
    printf("  -d, --delete <id>        Delete tenant with the specified ID\n");
    printf("  -s, --status <id>        Show tenant status\n");
    printf("  -l, --list               List all active tenants\n");
    printf("  -b, --bind <pid>         Bind process to tenant (use with --tenant)\n");
    printf("  -u, --unbind <pid>       Unbind process from tenant\n");
    printf("  -t, --tenant <id>        Specify tenant ID\n");
    printf("  -q, --quota <qp,mr,mem>  Set quota (QP,MR,Memory in MB)\n");
    printf("  -m, --monitor            Monitor tenant resource usage\n");
    printf("  -i, --interval <sec>     Monitoring interval (default: 5s)\n");
    printf("  -h, --help               Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s --create 1 --name \"Tenant A\" --quota 100,1000,1024\n", program);
    printf("  %s --bind 1234 --tenant 1\n", program);
    printf("  %s --list\n", program);
    printf("  %s --monitor\n", program);
}

// 创建租户
static int cmd_create_tenant(uint32_t tenant_id, const char *name, const char *quota_str) {
    tenant_quota_t quota = {
        .max_qp_per_tenant = 100,
        .max_mr_per_tenant = 1000,
        .max_memory_per_tenant = 1024ULL * 1024 * 1024,
        .max_cq_per_tenant = 100,
        .max_pd_per_tenant = 100
    };
    
    // 解析配额字符串 (format: qp,mr,mem_mb)
    if (quota_str) {
        unsigned int qp, mr, mem_mb;
        if (sscanf(quota_str, "%u,%u,%u", &qp, &mr, &mem_mb) == 3) {
            quota.max_qp_per_tenant = qp;
            quota.max_mr_per_tenant = mr;
            quota.max_memory_per_tenant = (uint64_t)mem_mb * 1024 * 1024;
        } else {
            fprintf(stderr, "Invalid quota format. Use: qp,mr,mem_mb\n");
            return -1;
        }
    }
    
    // 同时设置动态策略
    tenant_policy_t policy;
    memset(&policy, 0, sizeof(policy));
    policy.tenant_id = tenant_id;
    strncpy(policy.tenant_name, name ? name : "unnamed", sizeof(policy.tenant_name) - 1);
    policy.policy.qp_policy.limit.max_per_process = quota.max_qp_per_tenant;
    policy.policy.qp_policy.limit.max_global = quota.max_qp_per_tenant;
    policy.policy.mr_policy.limit.max_per_process = quota.max_mr_per_tenant;
    policy.policy.mr_policy.limit.max_global = quota.max_mr_per_tenant;
    policy.policy.memory_policy.limit.max_memory = quota.max_memory_per_tenant;
    dynamic_policy_set_tenant_policy(tenant_id, &policy);
    
    return tenant_create(tenant_id, name, &quota);
}

// 删除租户
static int cmd_delete_tenant(uint32_t tenant_id) {
    // 同时删除动态策略
    dynamic_policy_delete_tenant_policy(tenant_id);
    return tenant_delete(tenant_id);
}

// 显示租户状态
static int cmd_show_status(uint32_t tenant_id) {
    tenant_info_t info;
    
    if (tenant_get_info(tenant_id, &info) != 0) {
        fprintf(stderr, "Tenant %u not found or inactive\n", tenant_id);
        return -1;
    }
    
    printf("\n========== Tenant %u ==========\n", tenant_id);
    printf("Name: %s\n", info.tenant_name);
    printf("Status: %s\n", 
           info.status == TENANT_STATUS_ACTIVE ? "ACTIVE" : 
           (info.status == TENANT_STATUS_SUSPENDED ? "SUSPENDED" : "INACTIVE"));
    printf("\nResource Quota:\n");
    printf("  QP:  %d / %u\n", info.usage.qp_count, info.quota.max_qp_per_tenant);
    printf("  MR:  %d / %u\n", info.usage.mr_count, info.quota.max_mr_per_tenant);
    printf("  CQ:  %d / %u\n", info.usage.cq_count, info.quota.max_cq_per_tenant);
    printf("  PD:  %d / %u\n", info.usage.pd_count, info.quota.max_pd_per_tenant);
    printf("  Memory: %llu / %llu MB\n", 
           (unsigned long long)info.usage.memory_used / (1024 * 1024),
           (unsigned long long)info.quota.max_memory_per_tenant / (1024 * 1024));
    printf("\nProcess Count: %u\n", info.process_count);
    printf("Total QP Creates: %llu\n", (unsigned long long)info.usage.total_qp_creates);
    printf("Total MR Registrations: %llu\n", (unsigned long long)info.usage.total_mr_regs);
    printf("Created At: %s", ctime(&info.created_at));
    printf("Last Active: %s", ctime(&info.last_active_at));
    printf("================================\n\n");
    
    return 0;
}

// 列出所有租户
static int cmd_list_tenants(void) {
    tenant_info_t tenants[MAX_TENANTS];
    int count = tenant_get_active_list(tenants, MAX_TENANTS);
    
    if (count <= 0) {
        printf("No active tenants found.\n");
        return 0;
    }
    
    printf("\n========== Active Tenants ==========\n");
    printf("%-8s %-20s %-10s %-10s %-10s %-12s\n", 
           "ID", "Name", "Status", "QP", "MR", "Processes");
    printf("------------------------------------\n");
    
    for (int i = 0; i < count; i++) {
        printf("%-8u %-20s %-10s %-3d/%-6u %-3d/%-6u %-12u\n",
               tenants[i].tenant_id,
               tenants[i].tenant_name,
               tenants[i].status == TENANT_STATUS_ACTIVE ? "ACTIVE" : "SUSPENDED",
               tenants[i].usage.qp_count,
               tenants[i].quota.max_qp_per_tenant,
               tenants[i].usage.mr_count,
               tenants[i].quota.max_mr_per_tenant,
               tenants[i].process_count);
    }
    
    printf("====================================\n");
    printf("Total: %d active tenants\n\n", count);
    
    return 0;
}

// 绑定进程到租户
static int cmd_bind_process(pid_t pid, uint32_t tenant_id) {
    return tenant_bind_process(pid, tenant_id);
}

// 解绑进程
static int cmd_unbind_process(pid_t pid) {
    return tenant_unbind_process(pid);
}

// 监控租户资源使用
static int cmd_monitor(int interval_sec) {
    printf("\n========== Tenant Resource Monitor ==========\n");
    printf("Press Ctrl+C to stop\n\n");
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 打印表头
    printf("%-8s %-20s %-8s %-8s %-12s %-10s\n",
           "ID", "Name", "QP", "MR", "Memory(MB)", "Procs");
    printf("---------------------------------------------\n");
    
    while (g_running) {
        // 清除上一行
        printf("\033[2K\r");
        
        tenant_info_t tenants[MAX_TENANTS];
        int count = tenant_get_active_list(tenants, MAX_TENANTS);
        
        for (int i = 0; i < count; i++) {
            printf("%-8u %-20s %-3d/%-4u %-3d/%-4u %-6llu/%-5llu %-10u\n",
                   tenants[i].tenant_id,
                   tenants[i].tenant_name,
                   tenants[i].usage.qp_count,
                   tenants[i].quota.max_qp_per_tenant,
                   tenants[i].usage.mr_count,
                   tenants[i].quota.max_mr_per_tenant,
                   (unsigned long long)tenants[i].usage.memory_used / (1024 * 1024),
                   (unsigned long long)tenants[i].quota.max_memory_per_tenant / (1024 * 1024),
                   tenants[i].process_count);
        }
        
        if (count == 0) {
            printf("No active tenants.");
        }
        
        fflush(stdout);
        sleep(interval_sec);
        
        // 如果是多行输出，需要清屏重新打印
        if (count > 0) {
            printf("\033[%dA", count);
        }
    }
    
    printf("\n\nMonitoring stopped.\n");
    return 0;
}

int main(int argc, char *argv[]) {
    // 初始化
    if (tenant_shm_init() != 0) {
        fprintf(stderr, "Failed to initialize tenant shared memory\n");
        return 1;
    }
    
    if (dynamic_policy_init() != 0) {
        fprintf(stderr, "Failed to initialize dynamic policy\n");
        return 1;
    }
    
    // 解析命令行参数
    static struct option long_options[] = {
        {"create",   required_argument, 0, 'c'},
        {"name",     required_argument, 0, 'n'},
        {"delete",   required_argument, 0, 'd'},
        {"status",   required_argument, 0, 's'},
        {"list",     no_argument,       0, 'l'},
        {"bind",     required_argument, 0, 'b'},
        {"unbind",   required_argument, 0, 'u'},
        {"tenant",   required_argument, 0, 't'},
        {"quota",    required_argument, 0, 'q'},
        {"monitor",  no_argument,       0, 'm'},
        {"interval", required_argument, 0, 'i'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    int option_index = 0;
    uint32_t tenant_id = 0;
    pid_t pid = 0;
    const char *name = NULL;
    const char *quota_str = NULL;
    int interval = 5;
    
    int create_flag = 0, delete_flag = 0, status_flag = 0;
    int list_flag = 0, bind_flag = 0, unbind_flag = 0, monitor_flag = 0;
    
    while ((c = getopt_long(argc, argv, "c:n:d:s:lb:u:t:q:mi:h", 
                            long_options, &option_index)) != -1) {
        switch (c) {
            case 'c':
                create_flag = 1;
                tenant_id = atoi(optarg);
                break;
            case 'n':
                name = optarg;
                break;
            case 'd':
                delete_flag = 1;
                tenant_id = atoi(optarg);
                break;
            case 's':
                status_flag = 1;
                tenant_id = atoi(optarg);
                break;
            case 'l':
                list_flag = 1;
                break;
            case 'b':
                bind_flag = 1;
                pid = atoi(optarg);
                break;
            case 'u':
                unbind_flag = 1;
                pid = atoi(optarg);
                break;
            case 't':
                tenant_id = atoi(optarg);
                break;
            case 'q':
                quota_str = optarg;
                break;
            case 'm':
                monitor_flag = 1;
                break;
            case 'i':
                interval = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // 执行命令
    int ret = 0;
    
    if (create_flag) {
        if (tenant_id == 0) {
            fprintf(stderr, "Error: Tenant ID must be greater than 0\n");
            ret = 1;
        } else {
            ret = cmd_create_tenant(tenant_id, name, quota_str);
        }
    } else if (delete_flag) {
        ret = cmd_delete_tenant(tenant_id);
    } else if (status_flag) {
        ret = cmd_show_status(tenant_id);
    } else if (list_flag) {
        ret = cmd_list_tenants();
    } else if (bind_flag) {
        if (tenant_id == 0) {
            fprintf(stderr, "Error: --tenant required for bind operation\n");
            ret = 1;
        } else {
            ret = cmd_bind_process(pid, tenant_id);
        }
    } else if (unbind_flag) {
        ret = cmd_unbind_process(pid);
    } else if (monitor_flag) {
        ret = cmd_monitor(interval);
    } else {
        print_usage(argv[0]);
    }
    
    return ret;
}
