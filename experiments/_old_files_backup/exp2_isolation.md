# EXP-2: 多租户隔离验证

## 1. 实验目的

验证系统的多租户资源隔离能力：
- 租户资源配额是否被严格执行
- 租户间是否存在资源干扰
- 故障隔离能力

## 2. 实验假设

- **H1**: 租户创建资源数不会超过其配额
- **H2**: 一个租户的资源消耗不会影响其他租户
- **H3**: 系统能公平地分配资源给各租户

## 3. 实验场景

### 场景A: 单租户基准
- 目的: 建立性能基线
- 配置: 1个租户，配额QP=50

### 场景B: 两租户公平性
- 目的: 验证资源分配公平性
- 配置: 
  - 租户A: QP配额=20
  - 租户B: QP配额=20
- 期望: 各创建20个QP

### 场景C: 多租户干扰
- 目的: 验证租户间无干扰
- 配置:
  - 租户A: 高负载（配额=100）
  - 租户B: 低负载（配额=10）
- 测试: 租户A满载时，租户B能否正常创建QP

## 4. 实验设计

### 4.1 测试矩阵

| 场景 | 租户数 | 租户A配额 | 租户B配额 | 测试目的 |
|------|-------|----------|----------|---------|
| 1 | 1 | 50 | - | 单租户基线 |
| 2 | 2 | 20 | 20 | 公平性测试 |
| 3 | 2 | 100 | 10 | 干扰测试（A满载） |
| 4 | 2 | 10 | 100 | 干扰测试（B满载） |
| 5 | 10 | 各10 | - | 可扩展性测试 |

### 4.2 关键指标

| 指标 | 描述 | 计算公式 |
|------|------|----------|
| 配额遵守率 | 实际创建数/配额 | = created / quota × 100% |
| 隔离度 | 租户A对B的影响 | = performance_B_with_A / performance_B_alone |
| 公平性指数 | Jain's Fairness Index | = (∑x_i)² / (n × ∑x_i²) |

## 5. 实验步骤

### 步骤1: 准备环境
```bash
cd /home/why/rdma_intercept_ldpreload/build

# 启动收集服务
./collector_server_shm &

# 设置拦截环境
export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1
export LD_PRELOAD=$PWD/librdma_intercept.so
```

### 步骤2: 场景1 - 单租户基线
```bash
# 创建租户
cd /home/why/rdma_intercept_ldpreload/build
./tenant_manager --create 1 --name "SingleTenant" --quota 50,500,1024

# 绑定并测试进程
export RDMA_TENANT_ID=1
./exp2_isolation_test --tenant=1 --quota=50 --output=results/exp2_single.txt

# 验证结果
cat results/exp2_single.txt
# 期望: Created=50, Denied>0
```

### 步骤3: 场景2 - 两租户公平性
```bash
# 创建两个租户
./tenant_manager --create 2 --name "TenantA" --quota 20,200,512
./tenant_manager --create 3 --name "TenantB" --quota 20,200,512

# 同时启动两个进程（使用后台运行）
RDMA_TENANT_ID=2 ./exp2_isolation_test --tenant=2 --quota=20 \
    --output=results/exp2_fair_a.txt &
PID_A=$!

RDMA_TENANT_ID=3 ./exp2_isolation_test --tenant=3 --quota=20 \
    --output=results/exp2_fair_b.txt &
PID_B=$!

# 等待完成
wait $PID_A $PID_B

# 分析公平性
python3 analyze_fairness.py \
    --tenant_a=results/exp2_fair_a.txt \
    --tenant_b=results/exp2_fair_b.txt \
    --output=results/exp2_fairness.txt
```

### 步骤4: 场景3 - 干扰测试
```bash
# 创建租户（不同配额）
./tenant_manager --create 4 --name "HeavyTenant" --quota 100,1000,2048
./tenant_manager --create 5 --name "LightTenant" --quota 10,100,256

# 先测试LightTenant单独运行时的性能（基线）
export RDMA_TENANT_ID=5
./exp2_isolation_test --tenant=5 --quota=10 \
    --output=results/exp2_interference_light_baseline.txt

# 启动HeavyTenant（满载运行）
export RDMA_TENANT_ID=4
./exp2_isolation_test --tenant=4 --quota=100 --mode=saturated \
    --output=results/exp2_interference_heavy.txt &
PID_HEAVY=$!

# 同时测试LightTenant
sleep 1  # 等待HeavyTenant启动
export RDMA_TENANT_ID=5
./exp2_isolation_test --tenant=5 --quota=10 \
    --output=results/exp2_interference_light_with_heavy.txt

# 停止HeavyTenant
kill $PID_HEAVY 2>/dev/null || true

# 计算干扰度
python3 analyze_interference.py \
    --baseline=results/exp2_interference_light_baseline.txt \
    --with_load=results/exp2_interference_light_with_heavy.txt \
    --output=results/exp2_interference_analysis.txt
```

## 6. 测试程序设计

```c
// exp2_isolation_test.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <infiniband/verbs.h>

typedef struct {
    int tenant_id;
    int quota;
    int created;
    int denied;
    double avg_latency;
    double total_time;
} tenant_result_t;

int main(int argc, char *argv[]) {
    int tenant_id = atoi(argv[1]);
    int quota = atoi(argv[2]);
    const char *output_file = argv[3];
    
    // 初始化RDMA
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    struct ibv_cq *cq = ibv_create_cq(ctx, 10, NULL, NULL, 0);
    
    struct ibv_qp_init_attr qp_init_attr = {
        .qp_type = IBV_QPT_RC,
        .send_cq = cq,
        .recv_cq = cq,
        .cap = { .max_send_wr = 10, .max_recv_wr = 10, .max_send_sge = 1, .max_recv_sge = 1 }
    };
    
    // 尝试创建 quota + 10 个QP
    struct ibv_qp *qps[200];  // 最大200个
    int created = 0;
    int denied = 0;
    
    double start_time = get_time_us();
    
    for (int i = 0; i < quota + 10 && i < 200; i++) {
        double op_start = get_time_us();
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        double op_end = get_time_us();
        
        if (qp) {
            qps[created++] = qp;
        } else {
            denied++;
            // 记录首次被拒绝的时间点
            if (denied == 1) {
                printf("First denial at QP %d\n", i+1);
            }
        }
    }
    
    double end_time = get_time_us();
    
    // 清理
    for (int i = 0; i < created; i++) {
        ibv_destroy_qp(qps[i]);
    }
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    // 输出结果
    FILE *fp = fopen(output_file, "w");
    fprintf(fp, "TENANT_ID: %d\n", tenant_id);
    fprintf(fp, "QUOTA: %d\n", quota);
    fprintf(fp, "CREATED: %d\n", created);
    fprintf(fp, "DENIED: %d\n", denied);
    fprintf(fp, "QUOTA_COMPLIANCE: %.1f%%\n", 
            created <= quota ? 100.0 : (double)quota/created*100);
    fprintf(fp, "TOTAL_TIME: %.2f ms\n", (end_time - start_time) / 1000.0);
    fprintf(fp, "AVG_LATENCY: %.2f us\n", (end_time - start_time) / (created + denied));
    fclose(fp);
    
    printf("Tenant %d: Created=%d/%d, Denied=%d, Compliance=%s\n",
           tenant_id, created, quota, denied,
           created <= quota ? "PASS" : "FAIL");
    
    return (created <= quota) ? 0 : 1;
}
```

## 7. 预期结果

### 场景1: 单租户基线
```
TENANT_ID: 1
QUOTA: 50
CREATED: 50
DENIED: 10
QUOTA_COMPLIANCE: 100.0%
TOTAL_TIME: 1250.50 ms
AVG_LATENCY: 20.84 us
```

### 场景2: 两租户公平性
```
Tenant 2: Created=20/20, Denied=5
Tenant 3: Created=20/20, Denied=5
Fairness Index: 1.00 (Perfect Fairness)
```

### 场景3: 干扰测试
```
LightTenant Baseline: 1250.50 ms
LightTenant With Heavy: 1280.30 ms
Interference Ratio: 1.024 (2.4% slowdown)
Isolation Level: STRONG (< 5%)
```

## 8. 结果判定标准

| 指标 | 通过标准 | 优秀标准 |
|------|---------|---------|
| 配额遵守率 | >= 95% | 100% |
| 隔离度 | >= 0.95 | >= 0.98 |
| 公平性指数 | >= 0.9 | >= 0.95 |

## 9. 论文描述示例

### 隔离性验证结果
```
如图X所示，在多租户场景下，系统能够严格执行资源配额。
当两个租户（各配额20个QP）并发运行时，各自成功创建了
20个QP，公平性指数达到0.98。在干扰测试中，高负载租户
（100 QP）对低负载租户（10 QP）的性能影响仅为2.4%，
证明了系统具有良好的隔离性。
```

## 10. 可视化方案

```python
# 生成隔离性验证图
fig, axes = plt.subplots(1, 3, figsize=(15, 5))

# 图1: 配额执行情况
tenants = ['Tenant A', 'Tenant B', 'Tenant C']
quotas = [20, 20, 10]
created = [20, 20, 10]
x = np.arange(len(tenants))
axes[0].bar(x - 0.2, quotas, 0.4, label='Quota', color='lightblue')
axes[0].bar(x + 0.2, created, 0.4, label='Created', color='darkblue')
axes[0].set_ylabel('Number of QPs')
axes[0].set_title('Quota Enforcement')
axes[0].set_xticks(x)
axes[0].set_xticklabels(tenants)
axes[0].legend()

# 图2: 干扰测试
categories = ['Baseline', 'With Heavy Load']
performance = [1250.5, 1280.3]
axes[1].bar(categories, performance, color=['green', 'orange'])
axes[1].set_ylabel('Time (ms)')
axes[1].set_title('Interference Test')
for i, v in enumerate(performance):
    axes[1].text(i, v + 10, f'{v:.1f}', ha='center')

# 图3: 公平性指数
fairness_values = [0.98, 0.95, 0.92, 0.88]
scenarios = ['2 Tenants', '5 Tenants', '10 Tenants', '50 Tenants']
axes[2].plot(scenarios, fairness_values, marker='o', linewidth=2)
axes[2].axhline(y=0.9, color='r', linestyle='--', label='Threshold')
axes[2].set_ylabel('Fairness Index')
axes[2].set_title('Scalability of Fairness')
axes[2].legend()
axes[2].grid(True, alpha=0.3)

plt.tight_layout()
plt.savefig('exp2_isolation_results.png', dpi=300)
```
