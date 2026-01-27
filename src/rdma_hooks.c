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
bool collector_send_qp_create_event(void);
void collector_send_qp_destroy_event(void);
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
typedef struct ibv_mr *(*ibv_create_mr_fn)(struct ibv_pd *, void *);
typedef int (*ibv_destroy_mr_fn)(struct ibv_mr *);
typedef struct ibv_srq *(*ibv_create_srq_fn)(struct ibv_pd *, struct ibv_srq_init_attr *);
typedef int (*ibv_modify_srq_fn)(struct ibv_srq *, struct ibv_srq_attr *, int);
typedef int (*ibv_query_srq_fn)(struct ibv_srq *, struct ibv_srq_attr *);
typedef int (*ibv_destroy_srq_fn)(struct ibv_srq *);
typedef struct ibv_ah *(*ibv_create_ah_fn)(struct ibv_pd *, struct ibv_ah_attr *);
typedef int (*ibv_modify_ah_fn)(struct ibv_ah *, struct ibv_ah_attr *);
typedef int (*ibv_destroy_ah_fn)(struct ibv_ah *);

/* 原始函数指针存储 */
static ibv_create_qp_fn real_ibv_create_qp = NULL;
static ibv_create_qp_ex_fn real_ibv_create_qp_ex = NULL;
static ibv_destroy_qp_fn real_ibv_destroy_qp = NULL;
static ibv_create_cq_fn real_ibv_create_cq = NULL;
static ibv_destroy_cq_fn real_ibv_destroy_cq = NULL;
static ibv_alloc_pd_fn real_ibv_alloc_pd = NULL;
static ibv_dealloc_pd_fn real_ibv_dealloc_pd = NULL;
static ibv_dereg_mr_fn real_ibv_dereg_mr = NULL;
static ibv_create_mr_fn real_ibv_create_mr = NULL;
static ibv_destroy_mr_fn real_ibv_destroy_mr = NULL;
static ibv_create_srq_fn real_ibv_create_srq = NULL;
static ibv_modify_srq_fn real_ibv_modify_srq = NULL;
static ibv_query_srq_fn real_ibv_query_srq = NULL;
static ibv_destroy_srq_fn real_ibv_destroy_srq = NULL;
static ibv_create_ah_fn real_ibv_create_ah = NULL;
static ibv_modify_ah_fn real_ibv_modify_ah = NULL;
static ibv_destroy_ah_fn real_ibv_destroy_ah = NULL;

/* 静态初始化标志 */
static pthread_once_t hooks_init_once = PTHREAD_ONCE_INIT;

/* 初始化函数指针 */
static void init_function_pointers(void) {
    /* 首先调用init_if_needed确保拦截核心已初始化 */
    init_if_needed();
    
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
    real_ibv_dereg_mr = (ibv_dereg_mr_fn)dlsym(libibverbs, "ibv_dereg_mr");
    real_ibv_create_mr = (ibv_create_mr_fn)dlsym(libibverbs, "ibv_create_mr");
    real_ibv_destroy_mr = (ibv_destroy_mr_fn)dlsym(libibverbs, "ibv_destroy_mr");
    
    /* 尝试获取SRQ相关函数 */
    real_ibv_create_srq = (ibv_create_srq_fn)dlsym(libibverbs, "ibv_create_srq");
    real_ibv_modify_srq = (ibv_modify_srq_fn)dlsym(libibverbs, "ibv_modify_srq");
    real_ibv_query_srq = (ibv_query_srq_fn)dlsym(libibverbs, "ibv_query_srq");
    real_ibv_destroy_srq = (ibv_destroy_srq_fn)dlsym(libibverbs, "ibv_destroy_srq");
    
    /* 尝试获取AH相关函数 */
    real_ibv_create_ah = (ibv_create_ah_fn)dlsym(libibverbs, "ibv_create_ah");
    real_ibv_modify_ah = (ibv_modify_ah_fn)dlsym(libibverbs, "ibv_modify_ah");
    real_ibv_destroy_ah = (ibv_destroy_ah_fn)dlsym(libibverbs, "ibv_destroy_ah");
    
    /* 尝试获取扩展函数，但不强制要求 */
    real_ibv_create_qp_ex = (ibv_create_qp_ex_fn)dlsym(libibverbs, "ibv_create_qp_ex");
    
    /* 不关闭libibverbs句柄，因为函数指针仍然需要使用
     * 库会在程序退出时自动卸载
     */
    
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
    
    /* 输出SRQ函数指针信息 */
    if (real_ibv_create_srq) {
        fprintf(stderr, "[RDMA_HOOKS] ibv_create_srq: %p\n", real_ibv_create_srq);
    } else {
        fprintf(stderr, "[RDMA_HOOKS] WARNING: ibv_create_srq not found\n");
    }
    if (real_ibv_modify_srq) {
        fprintf(stderr, "[RDMA_HOOKS] ibv_modify_srq: %p\n", real_ibv_modify_srq);
    } else {
        fprintf(stderr, "[RDMA_HOOKS] WARNING: ibv_modify_srq not found\n");
    }
    if (real_ibv_query_srq) {
        fprintf(stderr, "[RDMA_HOOKS] ibv_query_srq: %p\n", real_ibv_query_srq);
    } else {
        fprintf(stderr, "[RDMA_HOOKS] WARNING: ibv_query_srq not found\n");
    }
    if (real_ibv_destroy_srq) {
        fprintf(stderr, "[RDMA_HOOKS] ibv_destroy_srq: %p\n", real_ibv_destroy_srq);
    } else {
        fprintf(stderr, "[RDMA_HOOKS] WARNING: ibv_destroy_srq not found\n");
    }
    
    /* 输出AH函数指针信息 */
    if (real_ibv_create_ah) {
        fprintf(stderr, "[RDMA_HOOKS] ibv_create_ah: %p\n", real_ibv_create_ah);
    } else {
        fprintf(stderr, "[RDMA_HOOKS] WARNING: ibv_create_ah not found\n");
    }
    if (real_ibv_modify_ah) {
        fprintf(stderr, "[RDMA_HOOKS] ibv_modify_ah: %p\n", real_ibv_modify_ah);
    } else {
        fprintf(stderr, "[RDMA_HOOKS] WARNING: ibv_modify_ah not found\n");
    }
    if (real_ibv_destroy_ah) {
        fprintf(stderr, "[RDMA_HOOKS] ibv_destroy_ah: %p\n", real_ibv_destroy_ah);
    } else {
        fprintf(stderr, "[RDMA_HOOKS] WARNING: ibv_destroy_ah not found\n");
    }
    
    /* 初始化动态策略 */
    init_dynamic_policy();
    fprintf(stderr, "[RDMA_HOOKS] Dynamic policy initialized\n");
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
    rdma_intercept_log(LOG_LEVEL_INFO, "开始检查QP创建限制");
    
    if (!g_intercept_state.config.enable_qp_control) {
        rdma_intercept_log(LOG_LEVEL_INFO, "QP控制未启用，允许创建");
        return true;  /* QP控制未启用，允许创建 */
    }
    
    rdma_intercept_log(LOG_LEVEL_INFO, "QP控制已启用，继续检查");
    
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
    rdma_intercept_log(LOG_LEVEL_INFO, "检查动态策略");
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
    
    rdma_intercept_log(LOG_LEVEL_INFO, "所有QP创建限制检查通过");
    return true;
}

/* 被拦截的ibv_create_qp函数 */
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_create_qp) {
        if (real_ibv_create_qp) {
            return real_ibv_create_qp(pd, qp_init_attr);
        }
        errno = ENOSYS;
        return NULL;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_create_qp: PD=%p", pd);
    }

    /* 检查资源限制 */
    if (!check_qp_creation_restrictions(pd, qp_init_attr)) {
        errno = EPERM;
        return NULL;
    }

    /* 调用原始函数 */
    struct ibv_qp *qp = real_ibv_create_qp(pd, qp_init_attr);
    
    if (qp) {
        /* 发送QP创建事件 */
        collector_send_qp_create_event();
        
        /* 更新QP计数（无锁操作，因为QP创建是串行的） */
        g_intercept_state.qp_count++;
        
        /* 记录QP创建信息（已优化，减少锁持有时间） */
        log_qp_creation(pd, qp_init_attr, qp, "ibv_create_qp");
        
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "QP created successfully: %p (current count: %d)", 
                             qp, g_intercept_state.qp_count);
        }
    } else {
        /* 错误信息总是记录 */
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
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_destroy_qp) {
        if (real_ibv_destroy_qp) {
            return real_ibv_destroy_qp(qp);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_destroy_qp: QP=%p", qp);
    }

    /* 调用原始函数 */
    int result = real_ibv_destroy_qp(qp);
    
    if (result == 0) {
        /* 发送QP销毁事件 */
        collector_send_qp_destroy_event();
        
        /* 更新QP计数（无锁操作，因为QP销毁是串行的） */
        if (g_intercept_state.qp_count > 0) {
            g_intercept_state.qp_count--;
        }
        
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "QP destroyed successfully: %p (current count: %d)", 
                             qp, g_intercept_state.qp_count);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to destroy QP: %s", strerror(errno));
    }

    return result;
}

/* 被拦截的ibv_create_ah函数 */
struct ibv_ah *ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *ah_attr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_create_ah) {
        if (real_ibv_create_ah) {
            return real_ibv_create_ah(pd, ah_attr);
        }
        errno = ENOSYS;
        return NULL;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_create_ah: PD=%p", pd);
    }

    /* 调用原始函数 */
    struct ibv_ah *ah = real_ibv_create_ah(pd, ah_attr);
    
    if (ah) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "AH created successfully: %p", ah);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to create AH: %s", strerror(errno));
    }

    return ah;
}

/* 被拦截的ibv_modify_ah函数 */
int ibv_modify_ah(struct ibv_ah *ah, struct ibv_ah_attr *ah_attr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_modify_ah) {
        if (real_ibv_modify_ah) {
            return real_ibv_modify_ah(ah, ah_attr);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_modify_ah: AH=%p", ah);
    }

    /* 调用原始函数 */
    int result = real_ibv_modify_ah(ah, ah_attr);
    
    if (result == 0) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "AH modified successfully: %p", ah);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to modify AH: %s", strerror(errno));
    }

    return result;
}

/* 被拦截的ibv_destroy_ah函数 */
int ibv_destroy_ah(struct ibv_ah *ah) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_destroy_ah) {
        if (real_ibv_destroy_ah) {
            return real_ibv_destroy_ah(ah);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_destroy_ah: AH=%p", ah);
    }

    /* 调用原始函数 */
    int result = real_ibv_destroy_ah(ah);
    
    if (result == 0) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "AH destroyed successfully: %p", ah);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to destroy AH: %s", strerror(errno));
    }

    return result;
}





/* 被拦截的ibv_create_srq函数 */
struct ibv_srq *ibv_create_srq(struct ibv_pd *pd, struct ibv_srq_init_attr *srq_init_attr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_create_srq) {
        if (real_ibv_create_srq) {
            return real_ibv_create_srq(pd, srq_init_attr);
        }
        errno = ENOSYS;
        return NULL;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_create_srq: PD=%p", pd);
    }

    /* 调用原始函数 */
    struct ibv_srq *srq = real_ibv_create_srq(pd, srq_init_attr);
    
    if (srq) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "SRQ created successfully: %p", srq);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to create SRQ: %s", strerror(errno));
    }

    return srq;
}

/* 被拦截的ibv_modify_srq函数 */
int ibv_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr, int attr_mask) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_modify_srq) {
        if (real_ibv_modify_srq) {
            return real_ibv_modify_srq(srq, srq_attr, attr_mask);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_modify_srq: SRQ=%p, attr_mask=%d", srq, attr_mask);
    }

    /* 调用原始函数 */
    int result = real_ibv_modify_srq(srq, srq_attr, attr_mask);
    
    if (result == 0) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "SRQ modified successfully: %p", srq);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to modify SRQ: %s", strerror(errno));
    }

    return result;
}

/* 被拦截的ibv_query_srq函数 */
int ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_query_srq) {
        if (real_ibv_query_srq) {
            return real_ibv_query_srq(srq, srq_attr);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_query_srq: SRQ=%p", srq);
    }

    /* 调用原始函数 */
    int result = real_ibv_query_srq(srq, srq_attr);
    
    if (result == 0) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "SRQ queried successfully: %p", srq);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to query SRQ: %s", strerror(errno));
    }

    return result;
}

/* 被拦截的ibv_destroy_srq函数 */
int ibv_destroy_srq(struct ibv_srq *srq) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_destroy_srq) {
        if (real_ibv_destroy_srq) {
            return real_ibv_destroy_srq(srq);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_destroy_srq: SRQ=%p", srq);
    }

    /* 调用原始函数 */
    int result = real_ibv_destroy_srq(srq);
    
    if (result == 0) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "SRQ destroyed successfully: %p", srq);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to destroy SRQ: %s", strerror(errno));
    }

    return result;
}

/* 被拦截的ibv_dereg_mr函数 */
int ibv_dereg_mr(struct ibv_mr *mr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_dereg_mr) {
        if (real_ibv_dereg_mr) {
            return real_ibv_dereg_mr(mr);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_dereg_mr: MR=%p", mr);
    }

    /* 调用原始函数 */
    int result = real_ibv_dereg_mr(mr);
    
    if (result == 0) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "MR deregistered successfully: %p", mr);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to deregister MR: %s", strerror(errno));
    }

    return result;
}

/* 被拦截的ibv_destroy_mr函数 */
int ibv_destroy_mr(struct ibv_mr *mr) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_destroy_mr) {
        if (real_ibv_destroy_mr) {
            return real_ibv_destroy_mr(mr);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_destroy_mr: MR=%p", mr);
    }

    /* 调用原始函数 */
    int result = real_ibv_destroy_mr(mr);
    
    if (result == 0) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "MR destroyed successfully: %p", mr);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to destroy MR: %s", strerror(errno));
    }

    return result;
}

/* 被拦截的ibv_create_cq函数 */
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe, void *cq_context,
                            struct ibv_comp_channel *channel, int comp_vector) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_create_cq) {
        if (real_ibv_create_cq) {
            return real_ibv_create_cq(context, cqe, cq_context, channel, comp_vector);
        }
        errno = ENOSYS;
        return NULL;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_create_cq: Context=%p, CQE=%d", context, cqe);
    }

    /* 调用原始函数 */
    struct ibv_cq *cq = real_ibv_create_cq(context, cqe, cq_context, channel, comp_vector);
    
    if (cq) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "CQ created successfully: %p (CQE=%d)", cq, cqe);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to create CQ: %s", strerror(errno));
    }

    return cq;
}

/* 被拦截的ibv_destroy_cq函数 */
int ibv_destroy_cq(struct ibv_cq *cq) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_destroy_cq) {
        if (real_ibv_destroy_cq) {
            return real_ibv_destroy_cq(cq);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_destroy_cq: CQ=%p", cq);
    }

    /* 调用原始函数 */
    int result = real_ibv_destroy_cq(cq);
    
    if (result == 0) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "CQ destroyed successfully: %p", cq);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to destroy CQ: %s", strerror(errno));
    }

    return result;
}

/* 被拦截的ibv_alloc_pd函数 */
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_alloc_pd) {
        if (real_ibv_alloc_pd) {
            return real_ibv_alloc_pd(context);
        }
        errno = ENOSYS;
        return NULL;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_alloc_pd: Context=%p", context);
    }

    /* 调用原始函数 */
    struct ibv_pd *pd = real_ibv_alloc_pd(context);
    
    if (pd) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "PD allocated successfully: %p", pd);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to allocate PD: %s", strerror(errno));
    }

    return pd;
}

/* 被拦截的ibv_dealloc_pd函数 */
int ibv_dealloc_pd(struct ibv_pd *pd) {
    pthread_once(&hooks_init_once, init_function_pointers);
    
    /* 快速路径：如果拦截未启用或函数指针无效，直接调用原始函数 */
    if (!rdma_intercept_is_enabled() || !real_ibv_dealloc_pd) {
        if (real_ibv_dealloc_pd) {
            return real_ibv_dealloc_pd(pd);
        }
        errno = ENOSYS;
        return -1;
    }

    /* 只在调试级别记录拦截信息 */
    if (g_intercept_state.config.log_level <= LOG_LEVEL_DEBUG) {
        rdma_intercept_log(LOG_LEVEL_DEBUG, "Intercepting ibv_dealloc_pd: PD=%p", pd);
    }

    /* 调用原始函数 */
    int result = real_ibv_dealloc_pd(pd);
    
    if (result == 0) {
        /* 只在信息级别及以下记录成功信息 */
        if (g_intercept_state.config.log_level <= LOG_LEVEL_INFO) {
            rdma_intercept_log(LOG_LEVEL_INFO, "PD deallocated successfully: %p", pd);
        }
    } else {
        /* 错误信息总是记录 */
        rdma_intercept_log(LOG_LEVEL_ERROR, "Failed to deallocate PD: %s", strerror(errno));
    }

    return result;
}