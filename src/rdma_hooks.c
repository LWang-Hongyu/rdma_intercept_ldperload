#define _GNU_SOURCE
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

// 前向声明
uint32_t collector_get_global_qp_count(void);
uint32_t collector_get_max_global_qp(void);
bool collector_check_global_qp_limit(void);
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

/* 原始函数指针存储 */
static ibv_create_qp_fn real_ibv_create_qp = NULL;
static ibv_create_qp_ex_fn real_ibv_create_qp_ex = NULL;
static ibv_destroy_qp_fn real_ibv_destroy_qp = NULL;
static ibv_create_cq_fn real_ibv_create_cq = NULL;
static ibv_destroy_cq_fn real_ibv_destroy_cq = NULL;
static ibv_alloc_pd_fn real_ibv_alloc_pd = NULL;
static ibv_dealloc_pd_fn real_ibv_dealloc_pd = NULL;

/* 静态初始化标志 */
static pthread_once_t hooks_init_once = PTHREAD_ONCE_INIT;

/* 初始化函数指针 */
static void init_function_pointers(void) {
    /* 直接打开libibverbs.so获取原始函数地址 */
    void *libibverbs = dlopen("libibverbs.so", RTLD_LAZY);
    if (!libibverbs) {
        fprintf(stderr, "[RDMA_HOOKS] Failed to open libibverbs.so: %s\n", dlerror());
        return;
    }
    
    /* 清除之前的错误 */
    dlerror();
    
    /* 尝试获取所有函数指针，但允许部分失败 */
    real_ibv_create_qp = (ibv_create_qp_fn)dlsym(libibverbs, "ibv_create_qp");
    real_ibv_destroy_qp = (ibv_destroy_qp_fn)dlsym(libibverbs, "ibv_destroy_qp");
    real_ibv_create_cq = (ibv_create_cq_fn)dlsym(libibverbs, "ibv_create_cq");
    real_ibv_destroy_cq = (ibv_destroy_cq_fn)dlsym(libibverbs, "ibv_destroy_cq");
    real_ibv_alloc_pd = (ibv_alloc_pd_fn)dlsym(libibverbs, "ibv_alloc_pd");
    real_ibv_dealloc_pd = (ibv_dealloc_pd_fn)dlsym(libibverbs, "ibv_dealloc_pd");
    
    /* 尝试获取扩展函数，但不强制要求 */
    real_ibv_create_qp_ex = (ibv_create_qp_ex_fn)dlsym(libibverbs, "ibv_create_qp_ex");
    
    /* 关闭libibverbs句柄 */
    dlclose(libibverbs);
    
    /* 使用printf而不是rdma_intercept_log，避免递归调用 */
    fprintf(stderr, "[RDMA_HOOKS] Function pointers initialized\n");
    if (real_ibv_create_qp) {
        fprintf(stderr, "[RDMA_HOOKS] ibv_create_qp: %p\n", real_ibv_create_qp);
    } else {
        fprintf(stderr, "[RDMA_HOOKS] WARNING: ibv_create_qp not found\n");
    }
    
    // 忽略ibv_create_qp_ex的错误，因为它可能是内联函数
    dlerror(); // 清除错误信息，防止影响后续dlsym调用
    if (real_ibv_create_qp_ex) {
        fprintf(stderr, "[RDMA_HOOKS] ibv_create_qp_ex: %p\n", real_ibv_create_qp_ex);
    } else {
        fprintf(stderr, "[RDMA_HOOKS] INFO: ibv_create_qp_ex not found (this is expected for inline functions)\n");
    }
    
    /* 初始化动态策略 */
    init_dynamic_policy();
    fprintf(stderr, "[RDMA_HOOKS] Dynamic policy initialized\n");
}

/* 检查函数指针是否有效 */
static bool check_function_pointers(void) {
    /* 至少需要基本的ibv_create_qp */
    return real_ibv_create_qp != NULL;
}

/* 记录QP创建信息 */
static void log_qp_creation(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr, 
                           struct ibv_qp *result_qp, const char *function_name) {
    if (!rdma_intercept_is_enabled() || !g_intercept_state.config.log_qp_creation) {
        return;
    }

    qp_creation_info_t info = {0};
    info.timestamp = time(NULL);
    info.pid = getpid();
    info.tid = pthread_self();
    info.context = pd->context;
    info.pd = pd;
    info.init_attr = qp_init_attr;
    info.qp_type = qp_init_attr->qp_type;
    info.max_send_wr = qp_init_attr->cap.max_send_wr;
    info.max_recv_wr = qp_init_attr->cap.max_recv_wr;
    info.max_send_sge = qp_init_attr->cap.max_send_sge;
    info.max_recv_sge = qp_init_attr->cap.max_recv_sge;
    info.max_inline_data = qp_init_attr->cap.max_inline_data;
    info.send_cq = qp_init_attr->send_cq;
    info.recv_cq = qp_init_attr->recv_cq;
    info.srq = qp_init_attr->srq;

    rdma_intercept_log(LOG_LEVEL_INFO, "QP Creation [%s]: PID=%d, QP=%p, Type=%d, "
                      "SendWR=%u, RecvWR=%u, SendSGE=%u, RecvSGE=%u, Inline=%u",
                      function_name, info.pid, result_qp, info.qp_type,
                      info.max_send_wr, info.max_recv_wr, info.max_send_sge, 
                      info.max_recv_sge, info.max_inline_data);

    rdma_intercept_log_qp_creation(&info);
}

/* 检查QP创建是否符合资源限制 */
static bool check_qp_creation_restrictions(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr) {
    if (!g_intercept_state.config.enable_qp_control) {
        return true;  /* QP控制未启用，允许创建 */
    }
    
    /* 检查QP类型限制 */
    switch (qp_init_attr->qp_type) {
        case IBV_QPT_RC:
            if (!g_intercept_state.config.allow_rc_qp) {
                rdma_intercept_log(LOG_LEVEL_ERROR, "RC QP creation denied by config");
                return false;
            }
            break;
        case IBV_QPT_UC:
            if (!g_intercept_state.config.allow_uc_qp) {
                rdma_intercept_log(LOG_LEVEL_ERROR, "UC QP creation denied by config");
                return false;
            }
            break;
        case IBV_QPT_UD:
            if (!g_intercept_state.config.allow_ud_qp) {
                rdma_intercept_log(LOG_LEVEL_ERROR, "UD QP creation denied by config");
                return false;
            }
            break;
        default:
            /* 其他QP类型（如RAW_PACKET, XRC等）默认允许 */
            rdma_intercept_log(LOG_LEVEL_DEBUG, "Allowing QP creation with type: %d", qp_init_attr->qp_type);
            break;
    }
    
    /* 检查动态策略（基于全局QP使用情况） */
    if (!check_dynamic_qp_policy(pd, qp_init_attr)) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "QP creation denied by dynamic policy");
        return false;
    }
    
    /* 检查本地QP数量限制 */
    if (g_intercept_state.qp_count >= g_intercept_state.config.max_qp_per_process) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "QP creation denied: max QP per process (%d) reached", 
                         g_intercept_state.config.max_qp_per_process);
        return false;
    }
    
    /* 检查WR限制 */
    if (qp_init_attr->cap.max_send_wr > g_intercept_state.config.max_send_wr_limit) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "QP creation denied: send WR limit (%d) exceeded (requested %d)", 
                         g_intercept_state.config.max_send_wr_limit, qp_init_attr->cap.max_send_wr);
        return false;
    }
    
    if (qp_init_attr->cap.max_recv_wr > g_intercept_state.config.max_recv_wr_limit) {
        rdma_intercept_log(LOG_LEVEL_ERROR, "QP creation denied: recv WR limit (%d) exceeded (requested %d)", 
                         g_intercept_state.config.max_recv_wr_limit, qp_init_attr->cap.max_recv_wr);
        return false;
    }
    
    return true;
}

/* 被拦截的ibv_create_qp函数 */
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    if (!rdma_intercept_is_enabled() || !check_function_pointers()) {
        if (real_ibv_create_qp) {
            return real_ibv_create_qp(pd, qp_init_attr);
        }
        errno = ENOSYS;
        return NULL;
    }

    rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_create_qp: PD=%p", pd);

    /* 检查资源限制 */
    if (!check_qp_creation_restrictions(pd, qp_init_attr)) {
        errno = EPERM;
        return NULL;
    }

    /* 调用原始函数 */
    struct ibv_qp *qp = real_ibv_create_qp(pd, qp_init_attr);
    
    if (qp) {
        /* 更新QP计数 */
        g_intercept_state.qp_count++;
        
        log_qp_creation(pd, qp_init_attr, qp, "ibv_create_qp");
        rdma_intercept_log(LOG_LEVEL_INFO, "QP created successfully: %p (current count: %d)", 
                         qp, g_intercept_state.qp_count);
    } else {
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to create QP: %s", strerror(errno));
    }

    return qp;
}

/* 注意：ibv_create_qp_ex是内联函数，无法直接拦截
 * 当comp_mask为IBV_QP_INIT_ATTR_PD时，它会调用ibv_create_qp
 * 我们已经拦截了ibv_create_qp，所以可以处理这种情况
 * 对于其他情况，我们需要依赖于驱动的实现
 */

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

    rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_destroy_qp: QP=%p", qp);

    /* 调用原始函数 */
    int result = real_ibv_destroy_qp(qp);
    
    if (result == 0) {
        /* 更新QP计数 */
        if (g_intercept_state.qp_count > 0) {
            g_intercept_state.qp_count--;
        }
        
        rdma_intercept_log(LOG_LEVEL_INFO, "QP destroyed successfully: %p (current count: %d)", 
                         qp, g_intercept_state.qp_count);
    } else {
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to destroy QP: %s", strerror(errno));
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

    rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_create_cq: Context=%p, CQE=%d", context, cqe);

    /* 调用原始函数 */
    struct ibv_cq *cq = real_ibv_create_cq(context, cqe, cq_context, channel, comp_vector);
    
    if (cq) {
        rdma_intercept_log(LOG_LEVEL_INFO, "CQ created successfully: %p (CQE=%d)", cq, cqe);
    } else {
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to create CQ: %s", strerror(errno));
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

    rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_destroy_cq: CQ=%p", cq);

    /* 调用原始函数 */
    int result = real_ibv_destroy_cq(cq);
    
    if (result == 0) {
        rdma_intercept_log(LOG_LEVEL_INFO, "CQ destroyed successfully: %p", cq);
    } else {
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to destroy CQ: %s", strerror(errno));
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

    rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_alloc_pd: Context=%p", context);

    /* 调用原始函数 */
    struct ibv_pd *pd = real_ibv_alloc_pd(context);
    
    if (pd) {
        rdma_intercept_log(LOG_LEVEL_INFO, "PD allocated successfully: %p", pd);
    } else {
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to allocate PD: %s", strerror(errno));
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

    rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_dealloc_pd: PD=%p", pd);

    /* 调用原始函数 */
    int result = real_ibv_dealloc_pd(pd);
    
    if (result == 0) {
        rdma_intercept_log(LOG_LEVEL_INFO, "PD deallocated successfully: %p", pd);
    } else {
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to deallocate PD: %s", strerror(errno));
    }

    return result;
}