# EXP-1: 微基准测试 - 拦截开销评估

## 1. 实验目的

评估LD_PRELOAD拦截机制引入的性能开销，包括：
- QP创建/销毁的延迟开销
- MR注册/注销的延迟开销
- 对整体吞吐量的影响

## 2. 实验假设

- **H1**: 拦截开销应小于20%
- **H2**: 缓存机制能有效降低重复查询开销
- **H3**: 批量更新能减少共享内存访问次数

## 3. 实验设计

### 3.1 测试变量

| 变量类型 | 名称 | 描述 | 取值 |
|---------|------|------|------|
| 自变量 | 拦截状态 | 是否启用拦截 | {无拦截, 有拦截} |
| 自变量 | 操作类型 | RDMA操作类型 | {QP_CREATE, QP_DESTROY, MR_REG, MR_DEREG} |
| 因变量 | 延迟 | 操作完成时间 | 微秒(us) |
| 因变量 | 吞吐量 | 每秒操作数 | ops/sec |
| 控制变量 | 迭代次数 | 测试重复次数 | 10,000次 |
| 控制变量 | 预热次数 | 排除冷启动影响 | 100次 |

### 3.2 实验步骤

#### 步骤1: 编译测试程序
```bash
cd /home/why/rdma_intercept_ldpreload/experiments
make exp1_microbenchmark
```

#### 步骤2: 基线测试（无拦截）
```bash
# 确保无拦截库加载
unset LD_PRELOAD

# 运行基线测试
./exp1_microbenchmark \
    --mode=baseline \
    --iterations=10000 \
    --warmup=100 \
    --output=results/exp1_baseline.txt
```

#### 步骤3: 拦截测试（有拦截）
```bash
# 设置拦截环境
export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1
export RDMA_INTERCEPT_MAX_QP_PER_PROCESS=100000  # 设置大值避免触发限制
export LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so

# 运行拦截测试
./exp1_microbenchmark \
    --mode=intercept \
    --iterations=10000 \
    --warmup=100 \
    --output=results/exp1_intercept.txt
```

#### 步骤4: 数据分析
```bash
python3 analyze_exp1.py \
    --baseline=results/exp1_baseline.txt \
    --intercept=results/exp1_intercept.txt \
    --output=results/exp1_analysis.txt
```

## 4. 测试代码设计

```c
// exp1_microbenchmark.c
// 关键代码结构

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <infiniband/verbs.h>

#define NUM_ITERATIONS 10000
#define NUM_WARMUP 100

typedef struct {
    double qp_create_latencies[NUM_ITERATIONS];
    double qp_destroy_latencies[NUM_ITERATIONS];
    double mr_reg_latencies[NUM_ITERATIONS];
    double mr_dereg_latencies[NUM_ITERATIONS];
} benchmark_results_t;

// 高精度计时
static inline double get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

// 预热阶段
void warmup(struct ibv_pd *pd, struct ibv_cq *cq) {
    struct ibv_qp_init_attr attr = {/*...*/};
    for (int i = 0; i < NUM_WARMUP; i++) {
        struct ibv_qp *qp = ibv_create_qp(pd, &attr);
        if (qp) ibv_destroy_qp(qp);
    }
}

// QP创建延迟测试
void benchmark_qp_create(struct ibv_pd *pd, struct ibv_cq *cq, 
                         double *latencies, int count) {
    struct ibv_qp_init_attr attr = {/*...*/};
    
    for (int i = 0; i < count; i++) {
        double start = get_time_us();
        struct ibv_qp *qp = ibv_create_qp(pd, &attr);
        double end = get_time_us();
        
        if (qp) {
            latencies[i] = end - start;
            ibv_destroy_qp(qp);  // 立即销毁，避免资源耗尽
        } else {
            latencies[i] = -1;  // 标记失败
        }
    }
}

// 统计分析
void analyze_results(double *data, int count, stats_t *stats) {
    // 计算均值、标准差、百分位数
    double sum = 0, sum_sq = 0;
    int valid_count = 0;
    
    for (int i = 0; i < count; i++) {
        if (data[i] > 0) {
            sum += data[i];
            sum_sq += data[i] * data[i];
            valid_count++;
        }
    }
    
    stats->mean = sum / valid_count;
    stats->std = sqrt(sum_sq / valid_count - stats->mean * stats->mean);
    
    // 排序计算百分位数
    qsort(data, count, sizeof(double), compare_double);
    stats->p50 = data[count * 0.5];
    stats->p95 = data[count * 0.95];
    stats->p99 = data[count * 0.99];
}
```

## 5. 预期结果

### 5.1 延迟对比（预期）

| 操作类型 | 基线延迟 | 拦截延迟 | 开销 | 是否可接受 |
|---------|---------|---------|------|----------|
| QP_CREATE | ~15 us | ~18 us | 20% | ✅ |
| QP_DESTROY | ~5 us | ~6 us | 20% | ✅ |
| MR_REG | ~20 us | ~23 us | 15% | ✅ |
| MR_DEREG | ~8 us | ~9 us | 12% | ✅ |

### 5.2 吞吐量对比（预期）

| 场景 | 吞吐量 (ops/sec) | 下降比例 |
|------|-----------------|---------|
| 无拦截 | 50,000 | - |
| 有拦截 | 42,000 | 16% |

## 6. 数据分析方法

### 6.1 开销计算公式
```
Overhead = (Latency_intercept - Latency_baseline) / Latency_baseline × 100%
```

### 6.2 统计检验
- 使用t检验验证差异显著性 (p < 0.05)
- 计算置信区间 (95%)
- 绘制CDF曲线展示延迟分布

### 6.3 可视化
```python
# 生成对比图
import matplotlib.pyplot as plt

fig, axes = plt.subplots(2, 2, figsize=(12, 10))

# 延迟对比柱状图
operations = ['QP_CREATE', 'QP_DESTROY', 'MR_REG', 'MR_DEREG']
baseline = [15, 5, 20, 8]
intercept = [18, 6, 23, 9]

x = np.arange(len(operations))
width = 0.35

axes[0, 0].bar(x - width/2, baseline, width, label='Baseline', color='green')
axes[0, 0].bar(x + width/2, intercept, width, label='With Intercept', color='red')
axes[0, 0].set_ylabel('Latency (us)')
axes[0, 0].set_title('Latency Comparison')
axes[0, 0].set_xticks(x)
axes[0, 0].set_xticklabels(operations)
axes[0, 0].legend()

# CDF图
axes[0, 1].ecdf(baseline_latencies, label='Baseline')
axes[0, 1].ecdf(intercept_latencies, label='With Intercept')
axes[0, 1].set_xlabel('Latency (us)')
axes[0, 1].set_ylabel('CDF')
axes[0, 1].set_title('Latency Distribution')
axes[0, 1].legend()

plt.tight_layout()
plt.savefig('exp1_results.png', dpi=300)
```

## 7. 异常处理

### 可能问题及解决方案

| 问题 | 现象 | 解决方案 |
|------|------|----------|
| 资源耗尽 | 创建QP失败 | 及时销毁创建的QP |
| 冷启动效应 | 前几次延迟较高 | 增加预热次数 |
| 系统噪声 | 数据波动大 | 多次运行取平均 |
| 权限问题 | 无法加载eBPF | 检查CAP_SYS_ADMIN |

## 8. 实验验证清单

- [ ] RDMA设备正常工作
- [ ] 拦截库编译成功
- [ ] 基线测试无拦截库
- [ ] 拦截测试有拦截库
- [ ] 迭代次数 >= 10000
- [ ] 预热次数 >= 100
- [ ] 数据文件生成成功
- [ ] 统计分析完成
- [ ] 图表生成清晰

## 9. 论文撰写要点

### 结果描述模板
```
如图X所示，在启用拦截机制后，QP创建操作的平均延迟从
15.2μs增加到18.3μs，开销为20.4%。通过缓存优化，第95
百分位延迟从22.5μs降低到19.8μs，表明缓存机制有效
降低了延迟波动。
```

### 关键发现
1. 拦截开销在可接受范围内 (< 20%)
2. 缓存机制有效降低延迟波动
3. 批量更新减少了共享内存访问开销
