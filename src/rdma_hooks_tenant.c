#define _GNU_SOURCE
/* NO_DEBUG: Disable debug output for performance testing */
#ifdef NO_DEBUG
  #define DEBUG_FPRINTF(...) ((void)0)
#else
  #define DEBUG_FPRINTF(...) fprintf(__VA_ARGS__)
#endif


/* 禁用调试日志 - EXP-1重新测试 */
#ifdef NO_DEBUG
  #define DISABLE_FPRINTF 1
#endif

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include "rdma_intercept.h"
#include "ebpf/ebpf_monitor_shm.h"
#include "shm/shared_memory.h"
#include "shm/shared_memory_tenant.h"
#include "dynamic_policy.h"

// 前向声明
uint32_t collector_get_global_qp_count(void);
uint32_t collector_get_max_global_qp(void);
bool collector_check_global_qp_limit(void);
bool collector_send_qp_create_event(void);
void collector_send_qp_destroy_event(void);
bool collector_send_mr_create_event(size_t length);
void collector_send_mr_destroy_event(size_t length);
bool collector_check_global_memory_limit(size_t requested_size);
bool check_dynamic_qp_policy(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);
void init_dynamic_policy(void);
void collector_cleanup(void);

/* 函数指针类型定义 */
typedef struct ibv_qp *(*ibv_create_qp_fn)(struct ibv_pd *, struct ibv_qp_init_attr *);
typedef struct ibv_qp *(*ibv_create_qp_ex_fn)(struct ibv_context *, struct ibv_qp_init_attr_ex *);
typedef int (*ibv_destroy_qp_fn)(struct ibv_qp *);
typedef struct ibv_cq *(*ibv_create_cq_fn)(struct ibv_context *, int, void *, struct ibv_comp_channel *, int);
typedef int (*ibv_destroy_cq_fn)(struct ibv_cq *);
typedef struct ibv_pd *(*ibv_alloc_pd_fn)(struct ibv_context *);
typedef int (*ibv_dealloc_pd_fn)(struct ibv_pd *);
typedef int (*ibv_dereg_mr_fn)(struct ibv_mr *);
typedef struct ibv_mr *(*ibv_reg_mr_fn)(struct ibv_pd *, void *, size_t, int);

/* 原始函数指针存储 */
static ibv_create_qp_fn real_ibv_create_qp = NULL;
static ibv_create_qp_ex_fn real_ibv_create_qp_ex = NULL;
static ibv_destroy_qp_fn real_ibv_destroy_qp = NULL;
static ibv_create_cq_fn real_ibv_create_cq = NULL;
static ibv_destroy_cq_fn real_ibv_destroy_cq = NULL;
static ibv_alloc_pd_fn real_ibv_alloc_pd = NULL;
static ibv_dealloc_pd_fn real_ibv_dealloc_pd = NULL;
static ibv_dereg_mr_fn real_ibv_dereg_mr = NULL;
static ibv_reg_mr_fn real_ibv_reg_mr = NULL;

/* 静态初始化标志 */
static pthread_once_t hooks_init_once = PTHREAD_ONCE_INIT;

/* eBPF初始化状态 */
static int ebpf_initialized = 0;
static int tenant_initialized = 0;

/* 通过共享内存获取进程资源使用情况 */
static int get_process_resources_via_shared_memory(int pid, resource_usage_t *usage)
{
    if (!usage) {
        return -1;
    }
    
    int result = shm_get_process_resources(pid, usage);
    if (result == 0) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] 从共享内存获取进程资源: PID=%d, QP=%d, MR=%d\n", 
                pid, usage->qp_count, usage->mr_count);
    } else {
        memset(usage, 0, sizeof(resource_usage_t));
    }
    
    return result;
}

/* 通过共享内存获取全局资源使用情况 */
static int get_global_resources_via_shared_memory(resource_usage_t *usage)
{
    if (!usage) {
        return -1;
    }
    
    int result = shm_get_global_resources(usage);
    if (result == 0) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] 从共享内存获取全局资源: QP=%d, MR=%d\n", 
                usage->qp_count, usage->mr_count);
    } else {
        memset(usage, 0, sizeof(resource_usage_t));
    }
    
    return result;
}

/* 获取进程的租户ID */
static uint32_t get_current_tenant_id(void) {
    if (!tenant_initialized) {
        return 0; // 默认租户
    }
    
    uint32_t tenant_id = 0;
    pid_t pid = getpid();
    
    if (tenant_get_process_tenant(pid, &tenant_id) != 0) {
        return 0; // 默认租户
    }
    
    return tenant_id;
}

/* 初始化函数指针 */
static void init_function_pointers(void) {
    init_if_needed();
    
    void *libibverbs = dlopen("libibverbs.so", RTLD_LAZY);
    if (!libibverbs) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] Failed to open libibverbs.so: %s\n", dlerror());
        return;
    }
    
    dlerror();
    
    real_ibv_create_qp = (ibv_create_qp_fn)dlsym(libibverbs, "ibv_create_qp");
    real_ibv_destroy_qp = (ibv_destroy_qp_fn)dlsym(libibverbs, "ibv_destroy_qp");
    real_ibv_create_cq = (ibv_create_cq_fn)dlsym(libibverbs, "ibv_create_cq");
    real_ibv_destroy_cq = (ibv_destroy_cq_fn)dlsym(libibverbs, "ibv_destroy_cq");
    real_ibv_alloc_pd = (ibv_alloc_pd_fn)dlsym(libibverbs, "ibv_alloc_pd");
    real_ibv_dealloc_pd = (ibv_dealloc_pd_fn)dlsym(libibverbs, "ibv_dealloc_pd");
    real_ibv_dereg_mr = (ibv_dereg_mr_fn)dlsym(libibverbs, "ibv_dereg_mr");
    real_ibv_reg_mr = (ibv_reg_mr_fn)dlsym(libibverbs, "ibv_reg_mr");
    
    real_ibv_create_qp_ex = (ibv_create_qp_ex_fn)dlsym(libibverbs, "ibv_create_qp_ex");
    
    DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] Function pointers initialized\n");
    
    /* 初始化eBPF监控 */
    int ebpf_err = ebpf_monitor_init();
    if (ebpf_err) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] eBPF monitor init warning: %d\n", ebpf_err);
        ebpf_initialized = 0;
    } else {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] eBPF monitor initialized\n");
        ebpf_initialized = 1;
    }
    
    /* 初始化租户共享内存 */
    if (tenant_shm_init() == 0) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] Tenant shared memory initialized\n");
        tenant_initialized = 1;
    } else {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] Tenant shared memory init warning\n");
        tenant_initialized = 0;
    }
    
    /* 初始化动态策略 */
    init_dynamic_policy();
    
    /* 绑定当前进程到租户（如果设置了环境变量） */
    const char *tenant_env = getenv("RDMA_TENANT_ID");
    if (tenant_env && tenant_initialized) {
        uint32_t tenant_id = atoi(tenant_env);
        if (tenant_id > 0) {
            pid_t pid = getpid();
            tenant_bind_process(pid, tenant_id);
            DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] Process %d bound to tenant %u\n", pid, tenant_id);
        }
    }
}

/* 更新租户资源计数 */
static void update_tenant_resource_count(uint32_t tenant_id, int resource_type, int delta) {
    if (tenant_id == 0 || !tenant_initialized) {
        return;
    }
    
    tenant_resource_usage_t usage;
    if (tenant_get_resource_usage(tenant_id, &usage) != 0) {
        return;
    }
    
    switch (resource_type) {
        case 0: // QP
            usage.qp_count += delta;
            if (delta > 0) usage.total_qp_creates++;
            else if (delta < 0) usage.total_qp_destroys++;
            break;
        case 1: // MR
            usage.mr_count += delta;
            if (delta > 0) usage.total_mr_regs++;
            else if (delta < 0) usage.total_mr_deregs++;
            break;
        case 3: // CQ
            usage.cq_count += delta;
            break;
        case 4: // PD
            usage.pd_count += delta;
            break;
    }
    
    tenant_update_resource_usage(tenant_id, &usage);
}

/* 检查QP创建是否符合租户资源限制 */
static bool check_tenant_qp_limit(uint32_t tenant_id) {
    if (tenant_id == 0 || !tenant_initialized) {
        return true; // 默认租户，不限制
    }
    
    // 直接从租户共享内存获取配额和使用情况
    tenant_info_t info;
    if (tenant_get_info(tenant_id, &info) != 0) {
        return false; // 无法获取信息，拒绝创建
    }
    
    if ((uint32_t)info.usage.qp_count >= info.quota.max_qp_per_tenant) {
        return false;
    }
    
    return true;
}

/* 检查MR创建是否符合租户资源限制 */
// check_tenant_mr_limit函数已内联到调用处
static inline bool check_tenant_mr_limit_inline(uint32_t tenant_id, size_t length) {
    DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] DEBUG MR: tenant_id=%u, tenant_initialized=%d\n",
            tenant_id, tenant_initialized);
    
    if (tenant_id == 0 || !tenant_initialized) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] DEBUG MR: allowing (default tenant or not initialized)\n");
        return true; // 默认租户，不限制
    }
    
    // 直接从租户共享内存获取配额和使用情况
    tenant_info_t info;
    if (tenant_get_info(tenant_id, &info) != 0) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] DEBUG MR: tenant_get_info failed\n");
        return false;
    }
    
    DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] DEBUG MR: tenant %u MR usage=%d/%d\n",
            tenant_id, info.usage.mr_count, info.quota.max_mr_per_tenant);
    
    // 检查MR数量限制
    if ((uint32_t)info.usage.mr_count >= info.quota.max_mr_per_tenant) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] 租户%d MR配额已用完 (%d/%d)\n", 
                tenant_id, info.usage.mr_count, info.quota.max_mr_per_tenant);
        return false;
    }
    
    // 检查内存限制 (如果配额为0则跳过)
    if (info.quota.max_memory_per_tenant > 0 && 
        info.usage.memory_used + length > info.quota.max_memory_per_tenant) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] 租户%d 内存配额不足 (%llu/%llu)\n", 
                tenant_id, (unsigned long long)info.usage.memory_used, 
                (unsigned long long)info.quota.max_memory_per_tenant);
        return false;
    }
    
    return true;
}

/* 检查QP创建是否符合资源限制 */
static bool check_qp_creation_restrictions(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr) {
    (void)pd;
    
    DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] DEBUG: enable_qp_control=%d, enable_intercept=%d\n", 
            g_intercept_state.config.enable_qp_control, g_intercept_state.config.enable_intercept);
    
    if (!g_intercept_state.config.enable_qp_control) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] DEBUG: QP control disabled, skipping checks\n");
        return true;
    }
    
    /* 检查QP类型限制 */
    switch (qp_init_attr->qp_type) {
        case IBV_QPT_RC:
            if (!g_intercept_state.config.allow_rc_qp) {
                DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] RC QP creation denied\n");
                return false;
            }
            break;
        case IBV_QPT_UC:
            if (!g_intercept_state.config.allow_uc_qp) {
                DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] UC QP creation denied\n");
                return false;
            }
            break;
        case IBV_QPT_UD:
            if (!g_intercept_state.config.allow_ud_qp) {
                DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] UD QP creation denied\n");
                return false;
            }
            break;
        default:
            break;
    }
    
    uint32_t tenant_id = get_current_tenant_id();
    
    /* 检查租户限制（直接从租户共享内存获取） */
    if (!check_tenant_qp_limit(tenant_id)) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] QP creation denied: tenant %u QP limit reached\n", tenant_id);
        return false;
    }
    
    /* 注意：动态策略检查暂时跳过，因为策略数组不在共享内存中
     * 租户限制已通过 tenant_shm 进行检查
     */
    // if (dynamic_policy_check_tenant_limit(tenant_id, RESOURCE_QP, 0, 1)) {
    //     DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] QP creation denied: dynamic policy limit\n");
    //     return false;
    // }
    
    /* 获取进程资源使用情况 */
    resource_usage_t proc_usage;
    int pid = getpid();
    int collector_err = get_process_resources_via_shared_memory(pid, &proc_usage);
    
    if (collector_err == 0) {
        uint32_t effective_qp_count = (uint32_t)proc_usage.qp_count;
        if ((effective_qp_count + 1) > g_intercept_state.config.max_qp_per_process) {
            DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] QP creation denied: per-process limit\n");
            return false;
        }
    }
    
    /* 检查全局限制 */
    resource_usage_t global_usage;
    int global_err = get_global_resources_via_shared_memory(&global_usage);
    if (global_err == 0) {
        if ((uint32_t)global_usage.qp_count >= g_intercept_state.config.max_global_qp) {
            DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] QP creation denied: global limit\n");
            return false;
        }
    }
    
    return true;
}

/* 被拦截的ibv_create_qp函数 */
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    if (!rdma_intercept_is_enabled() || !real_ibv_create_qp) {
        if (real_ibv_create_qp) {
            return real_ibv_create_qp(pd, qp_init_attr);
        }
        errno = ENOSYS;
        return NULL;
    }

    if (!check_qp_creation_restrictions(pd, qp_init_attr)) {
        errno = EPERM;
        return NULL;
    }

    struct ibv_qp *qp = real_ibv_create_qp(pd, qp_init_attr);
    
    if (qp) {
        pthread_mutex_lock(&g_intercept_state.resource_mutex);
        g_intercept_state.qp_count++;
        pthread_mutex_unlock(&g_intercept_state.resource_mutex);
        
        /* 更新共享内存 */
        resource_usage_t new_usage;
        int pid = getpid();
        new_usage.qp_count = g_intercept_state.qp_count;
        new_usage.mr_count = g_intercept_state.mr_count;
        new_usage.memory_used = g_intercept_state.memory_used;
        shm_update_process_resources(pid, &new_usage);
        
        /* 更新租户资源 */
        update_tenant_resource_count(get_current_tenant_id(), 0, 1); // 0=QP
        
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] QP created: %p\n", qp);
    }

    return qp;
}

/* 被拦截的ibv_destroy_qp函数 */
int ibv_destroy_qp(struct ibv_qp *qp) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    if (!rdma_intercept_is_enabled() || !real_ibv_destroy_qp) {
        if (real_ibv_destroy_qp) {
            return real_ibv_destroy_qp(qp);
        }
        errno = ENOSYS;
        return -1;
    }

    int result = real_ibv_destroy_qp(qp);
    
    if (result == 0) {
        pthread_mutex_lock(&g_intercept_state.resource_mutex);
        if (g_intercept_state.qp_count > 0) {
            g_intercept_state.qp_count--;
        }
        pthread_mutex_unlock(&g_intercept_state.resource_mutex);
        
        /* 更新租户资源 */
        update_tenant_resource_count(get_current_tenant_id(), 0, -1); // 0=QP
        
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] QP destroyed: %p\n", qp);
    }

    return result;
}

/* 被拦截的ibv_reg_mr函数 - 使用不同名称避免宏冲突 */
struct ibv_mr *__real_ibv_reg_mr_tenant(struct ibv_pd *pd, void *addr, size_t length, int access) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    if (!rdma_intercept_is_enabled() || !real_ibv_reg_mr) {
        if (real_ibv_reg_mr) {
            return real_ibv_reg_mr(pd, addr, length, access);
        }
        errno = ENOSYS;
        return NULL;
    }

    uint32_t tenant_id = get_current_tenant_id();
    
    /* 检查租户MR限制 */
    if (!check_tenant_mr_limit_inline(tenant_id, length)) {
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] MR registration denied: tenant %u limit\n", tenant_id);
        errno = EPERM;
        return NULL;
    }

    struct ibv_mr *mr = real_ibv_reg_mr(pd, addr, length, access);
    
    if (mr) {
        pthread_mutex_lock(&g_intercept_state.resource_mutex);
        g_intercept_state.mr_count++;
        g_intercept_state.memory_used += length;
        pthread_mutex_unlock(&g_intercept_state.resource_mutex);
        
        /* 更新租户资源 */
        update_tenant_resource_count(tenant_id, 1, 1); // 1=MR
        
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] MR registered: %p, length=%zu\n", mr, length);
    }

    return mr;
}

/* 被拦截的ibv_dereg_mr函数 - 使用不同名称避免宏冲突 */
int __real_ibv_dereg_mr_tenant(struct ibv_mr *mr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    if (!rdma_intercept_is_enabled() || !real_ibv_dereg_mr) {
        if (real_ibv_dereg_mr) {
            return real_ibv_dereg_mr(mr);
        }
        errno = ENOSYS;
        return -1;
    }

    size_t mr_length = mr ? mr->length : 0;
    int result = real_ibv_dereg_mr(mr);
    
    if (result == 0) {
        pthread_mutex_lock(&g_intercept_state.resource_mutex);
        if (g_intercept_state.mr_count > 0) {
            g_intercept_state.mr_count--;
        }
        if (g_intercept_state.memory_used >= mr_length) {
            g_intercept_state.memory_used -= mr_length;
        }
        pthread_mutex_unlock(&g_intercept_state.resource_mutex);
        
        /* 更新租户资源 */
        update_tenant_resource_count(get_current_tenant_id(), 1, -1); // 1=MR
        
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] MR deregistered: %p\n", mr);
    }

    return result;
}

/* 被拦截的ibv_create_cq函数 */
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe, void *cq_context,
                            struct ibv_comp_channel *channel, int comp_vector) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    if (!rdma_intercept_is_enabled() || !real_ibv_create_cq) {
        if (real_ibv_create_cq) {
            return real_ibv_create_cq(context, cqe, cq_context, channel, comp_vector);
        }
        errno = ENOSYS;
        return NULL;
    }

    struct ibv_cq *cq = real_ibv_create_cq(context, cqe, cq_context, channel, comp_vector);
    
    if (cq) {
        /* 更新租户资源 */
        update_tenant_resource_count(get_current_tenant_id(), 3, 1); // 3=CQ
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] CQ created: %p\n", cq);
    }

    return cq;
}

/* 被拦截的ibv_destroy_cq函数 */
int ibv_destroy_cq(struct ibv_cq *cq) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    if (!rdma_intercept_is_enabled() || !real_ibv_destroy_cq) {
        if (real_ibv_destroy_cq) {
            return real_ibv_destroy_cq(cq);
        }
        errno = ENOSYS;
        return -1;
    }

    int result = real_ibv_destroy_cq(cq);
    
    if (result == 0) {
        /* 更新租户资源 */
        update_tenant_resource_count(get_current_tenant_id(), 3, -1); // 3=CQ
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] CQ destroyed: %p\n", cq);
    }

    return result;
}

/* 被拦截的ibv_alloc_pd函数 */
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    if (!rdma_intercept_is_enabled() || !real_ibv_alloc_pd) {
        if (real_ibv_alloc_pd) {
            return real_ibv_alloc_pd(context);
        }
        errno = ENOSYS;
        return NULL;
    }

    struct ibv_pd *pd = real_ibv_alloc_pd(context);
    
    if (pd) {
        /* 更新租户资源 */
        update_tenant_resource_count(get_current_tenant_id(), 4, 1); // 4=PD
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] PD allocated: %p\n", pd);
    }

    return pd;
}

/* 被拦截的ibv_dealloc_pd函数 */
int ibv_dealloc_pd(struct ibv_pd *pd) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    if (!rdma_intercept_is_enabled() || !real_ibv_dealloc_pd) {
        if (real_ibv_dealloc_pd) {
            return real_ibv_dealloc_pd(pd);
        }
        errno = ENOSYS;
        return -1;
    }

    int result = real_ibv_dealloc_pd(pd);
    
    if (result == 0) {
        /* 更新租户资源 */
        update_tenant_resource_count(get_current_tenant_id(), 4, -1); // 4=PD
        DEBUG_FPRINTF(stderr, "[RDMA_HOOKS_TENANT] PD deallocated: %p\n", pd);
    }

    return result;
}

/* LD_PRELOAD使用的实际拦截函数 - 包装租户检查函数
 * 注意：需要在包含verbs.h之前#undef ibv_reg_mr，因为它是一个宏
 */
#ifdef ibv_reg_mr
#undef ibv_reg_mr
#endif
struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int access) {
    return __real_ibv_reg_mr_tenant(pd, addr, length, access);
}

#ifdef ibv_dereg_mr
#undef ibv_dereg_mr
#endif
int ibv_dereg_mr(struct ibv_mr *mr) {
    return __real_ibv_dereg_mr_tenant(mr);
}
