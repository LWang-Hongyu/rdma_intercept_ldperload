# RDMA Intercept 实验报告

**实验日期**: 2026-04-10  
**实验机器**: 
- 本机 (guolab-4): 管理IP 10.157.195.92, RDMA IP 192.10.10.104
- 对端 (guolab-5): 管理IP 10.157.195.93, RDMA IP 192.10.10.105

---

## 实验完成情况总结

### 已完成的实验

| 实验 | 描述 | 状态 | 结果 |
|------|------|------|------|
| EXP-1 | 微基准测试 - 拦截开销评估 | ✅ 完成 | 通过 |
| EXP-8 | QP资源隔离验证 | ✅ 完成 | 通过 |
| EXP-9 | MR资源隔离验证 | ⚠️ 部分完成 | 需要修复代码 |

---

## EXP-1: 微基准测试

### 实验目的
评估LD_PRELOAD拦截机制引入的性能开销

### 实验结果

| 指标 | 基线 (无拦截) | 拦截 (有LD_PRELOAD) | 开销 |
|------|---------------|---------------------|------|
| QP创建延迟 (MEAN) | 340.03 μs | 332.44 μs | -2.00% |
| QP创建延迟 (P95) | 403.41 μs | 395.30 μs | -2.01% |
| QP创建延迟 (P99) | 513.76 μs | 483.54 μs | -5.88% |
| MR注册延迟 (MEAN) | 15.69 μs | 15.54 μs | -0.96% |

### 结论
✅ **测试通过** - 拦截开销小于20%，满足设计要求

- QP创建拦截实际上略微降低了延迟（可能是由于缓存效应）
- MR注册拦截开销几乎为零
- 系统性能影响在可接受范围内

### 图表
见 `exp1_microbenchmark/figures/exp1_microbenchmark_v2.png`

---

## EXP-8: QP资源隔离验证

### 实验目的
验证拦截系统能否正确执行QP配额限制

### 实验配置
- 配额限制: 每租户10个QP
- 请求数量: 50个QP

### 实验结果

| 场景 | 请求QP数 | 成功 | 失败 | 成功率 |
|------|----------|------|------|--------|
| 基线 (无拦截) | 50 | 50 | 0 | 100% |
| 拦截 (限制=10) | 50 | 10 | 40 | 20% |

### 结论
✅ **测试通过** - 拦截系统正确执行了QP配额限制

- 超出配额的40个QP被成功拦截
- 资源隔离功能正常工作
- 拦截决策延迟约 524 μs（可接受）

### 图表
见 `exp8_qp_isolation/figures/exp8_qp_isolation.png`

---

## EXP-9: MR资源隔离验证

### 实验目的
验证拦截系统能否正确执行MR配额限制

### 实验状态
⚠️ **部分完成** - 发现代码问题

### 问题分析
当前 `rdma_hooks_tenant.c` 中的MR限制检查依赖于租户系统：
- 只检查租户级别的MR配额 (`check_tenant_mr_limit_inline`)
- 未实现基于进程的全局MR限制检查
- 当租户未激活时，限制检查返回true（不限制）

### 建议修复
需要在 `rdma_hooks_tenant.c` 的 `__real_ibv_reg_mr_tenant` 函数中添加：
1. 检查 `g_intercept_state.config.enable_mr_control`
2. 检查 `g_intercept_state.mr_count < g_intercept_state.config.max_mr_per_process`

---

## 网络环境检查

### 管理网络
- 本机管理IP: 10.157.195.92 ✅
- 对端管理IP: 10.157.195.93 ✅
- SSH连接: 正常 ✅
- Ping测试: 正常 (0.28ms) ✅

### RDMA网络
- 本机RDMA IP: 192.10.10.104 ✅
- 对端RDMA IP: 192.10.10.105 ✅
- RDMA设备: mlx5_0 (Active, 100G) ✅
- Ping测试: 正常 (0.24ms) ✅

### 外网连接
- pip3外网访问: 受限 ❌
- 解决方案: 使用清华镜像源 ✅
- matplotlib/numpy: 安装成功 ✅

---

## 实验结果文件

```
experiments/
├── exp1_microbenchmark/
│   ├── results/
│   │   ├── baseline.csv           # 基线测试结果
│   │   └── with_intercept.csv     # 拦截测试结果
│   └── figures/
│       └── exp1_microbenchmark_v2.png  # 可视化图表
│
├── exp8_qp_isolation/
│   ├── results/
│   │   ├── baseline.txt           # 基线测试结果
│   │   └── with_limit.txt         # 限制测试结果
│   └── figures/
│       └── exp8_qp_isolation.png  # 可视化图表
│
└── exp9_mr_isolation/
    └── results/
        ├── baseline.txt
        └── with_limit.txt
```

---

## 总结

### 已验证功能
1. ✅ **拦截系统基本功能**: LD_PRELOAD正确拦截RDMA调用
2. ✅ **性能开销**: 小于20%，满足设计要求
3. ✅ **QP资源隔离**: 配额限制正确执行

### 待修复问题
1. ⚠️ **MR资源隔离**: 需要添加进程级MR限制检查
2. ⚠️ **租户管理**: 需要预先激活租户才能使用租户级限制

### 下一步建议
1. 修复 `rdma_hooks_tenant.c` 中的MR限制检查逻辑
2. 添加租户自动激活功能或文档说明
3. 运行双机实验（MR注销攻击测试）
4. 运行更多多租户隔离实验

---

*报告生成时间: 2026-04-10*
