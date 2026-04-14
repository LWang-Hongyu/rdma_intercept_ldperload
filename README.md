# 基于LD_PRELOAD的RDMA资源隔离与控制系统

基于LD_PRELOAD技术和共享内存的高性能RDMA资源隔离与控制系统，实现多租户环境下的细粒度资源配额管理和性能隔离。

## 项目概述

本项目旨在解决多租户环境下RDMA资源竞争问题，通过以下核心技术实现资源隔离：

- **LD_PRELOAD拦截**：动态库注入实现用户态拦截，无内核依赖
- **共享内存通信**：使用共享内存实现高效进程间通信
- **租户管理**：基于租户的资源配额管理和隔离
- **实时控制**：基于共享内存数据的实时资源限制
- **线程安全**：多进程并发访问的同步机制

## 系统架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           系统架构图                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐  │
│  │   Application   │────▶│ librdma_intercept│────▶│  tenant_manager │  │
│  │   (用户应用)     │     │   (拦截库)        │     │   _daemon       │  │
│  └─────────────────┘     └──────────────────┘     │   (租户管理)      │  │
│                               │                    └────────┬────────┘  │
│                               │                             │           │
│                               ▼                             ▼           │
│                        ┌─────────────┐              ┌─────────────┐     │
│                        │ Shared Mem  │◀────────────▶│ Shared Mem  │     │
│                        │(进程资源)    │              │(租户配额)     │     │
│                        └─────────────┘              └─────────────┘     │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                         数据流向                                  │   │
│  │  1. 应用调用RDMA API ──▶ 拦截库拦截                               │   │
│  │  2. 拦截库检查共享内存中的租户配额                                 │   │
│  │  3. 符合配额 ──▶ 允许操作 ──▶ 更新资源计数                         │   │
│  │  4. 超出配额 ──▶ 拒绝操作 ──▶ 返回错误                             │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 核心组件

#### 1. 动态拦截库 (`librdma_intercept.so`)
- **文件**: `src/rdma_hooks.c`, `src/rdma_hooks_tenant.c`
- **功能**: 
  - LD_PRELOAD注入用户程序
  - 拦截RDMA API调用（QP/MR创建销毁）
  - 通过共享内存查询租户配额并执行访问控制
  - 支持租户级资源限制
  - 直接更新共享内存中的资源计数

#### 2. 租户管理守护进程 (`tenant_manager_daemon`)
- **文件**: `src/tenant_manager_daemon.c`
- **功能**:
  - 统一管理租户配额和限制策略
  - 维护全局资源使用统计
  - 提供租户创建、更新、删除接口
  - 支持动态配额调整

#### 3. 租户管理客户端 (`tenant_manager_client`)
- **文件**: `src/tenant_manager_client.c`
- **功能**:
  - 命令行工具管理租户
  - 支持创建、更新、删除、查询租户
  - 用于实验和运维管理

#### 4. 共享内存管理 (`shared_memory`)
- **文件**: `src/shm/shared_memory.c`, `src/shm/shared_memory_tenant.c`
- **功能**:
  - 提供高效进程间通信机制
  - 存储租户资源配额和统计数据
  - 支持多进程安全访问的同步机制
  - 实现零拷贝、低延迟的数据交换

#### 5. 性能优化器 (`performance_optimizer`)
- **文件**: `src/performance_optimizer.c`
- **功能**:
  - 进程级本地缓存，减少共享内存访问
  - 自适应TTL机制
  - 批量操作优化

#### 6. 配置管理 (`config`)
- **文件**: `src/config.c`
- **功能**:
  - 环境变量配置解析
  - 运行时参数管理

#### 7. 日志系统 (`logger`)
- **文件**: `src/logger.c`
- **功能**:
  - 详细的拦截和监控日志
  - 多级别日志支持

## 功能特性

### 资源限制
- **租户级QP限制**：控制每个租户的QP数量
- **租户级MR限制**：控制每个租户的MR数量和内存使用
- **动态策略**：支持运行时调整资源配额
- **热更新**：无需重启应用即可更新配额

### 监控能力
- **实时监控**：基于共享内存的低开销监控
- **细粒度统计**：按租户维度统计资源使用
- **全局视图**：系统级资源使用概览
- **性能指标**：资源使用趋势和峰值统计

### 高效通信
- **共享内存**：零拷贝、低延迟的数据交换
- **本地缓存**：进程级缓存减少共享内存访问
- **同步机制**：自旋锁确保多进程安全访问
- **内存映射**：高效的数据共享方式

### 配置管理
- **环境变量配置**：灵活的运行时配置
- **动态策略更新**：支持运行时策略调整
- **日志记录**：详细的拦截和监控日志
- **错误处理**：完善的异常处理机制

## 目录结构

```
rdma_intercept_ldpreload/
├── src/                          # 源代码目录
│   ├── rdma_hooks.c              # RDMA钩子函数（基础拦截）
│   ├── rdma_hooks_tenant.c       # RDMA钩子函数（租户级拦截）
│   ├── intercept_core.c          # 拦截核心逻辑
│   ├── tenant_manager_daemon.c   # 租户管理守护进程
│   ├── tenant_manager_client.c   # 租户管理客户端
│   ├── tenant_manager.c          # 租户管理通用函数
│   ├── performance_optimizer.c   # 性能优化器
│   ├── config.c                  # 配置管理
│   ├── logger.c                  # 日志系统
│   ├── collector_client.c        # 数据收集客户端
│   ├── collector_server.c        # 数据收集服务端
│   ├── collector_server_shm.c    # 基于共享内存的数据收集
│   ├── dynamic_policy.c          # 动态策略
│   ├── dynamic_policy_manager.c  # 动态策略管理器
│   └── shm/                      # 共享内存模块
│       ├── shared_memory.c       # 共享内存基础功能
│       ├── shared_memory.h       # 共享内存头文件
│       ├── shared_memory_tenant.c # 租户级共享内存
│       └── shared_memory_tenant.h # 租户级共享内存头文件
├── include/                      # 头文件目录
│   └── rdma_intercept.h          # 主头文件
├── experiments/                  # 实验目录
│   ├── exp1_microbenchmark/      # 微基准测试
│   ├── exp2_multi_tenant_isolation/ # 多租户隔离测试
│   ├── exp3_scalability/         # 可扩展性测试
│   ├── exp4_cache/               # 缓存性能测试
│   ├── exp5_dynamic_policy/      # 动态策略测试
│   ├── exp6_bandwidth_impact/    # 带宽影响测试
│   ├── exp7_bandwidth_isolation/ # 带宽隔离测试
│   ├── exp8_qp_isolation/        # QP隔离测试
│   ├── exp9_mr_isolation/        # MR隔离测试
│   └── exp_mr_dereg/             # MR注销攻击防护测试
├── build/                        # 构建输出目录
├── CMakeLists.txt                # CMake配置文件
└── README.md                     # 本文档
```

## 快速开始

### 1. 构建项目

```bash
cd /home/why/rdma_intercept_ldpreload
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 2. 启动租户管理守护进程

```bash
cd build
sudo ./tenant_manager_daemon --daemon --foreground
```

### 3. 创建租户并设置配额

```bash
cd build
# 创建租户10，QP限制=10，MR限制=10，内存限制=1GB
sudo ./tenant_manager_client create 10 10 10 1073741824 "TestTenant"

# 查看租户列表
sudo ./tenant_manager_client list
```

### 4. 运行受保护的应用

```bash
export LD_PRELOAD=/home/why/rdma_intercept_ldpreload/build/librdma_intercept.so
export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1
export RDMA_INTERCEPT_ENABLE_MR_CONTROL=1
export RDMA_TENANT_ID=10

# 运行RDMA应用
ib_write_bw -d mlx5_0 -x 2
```

## 配置参数

### 环境变量

| 参数 | 描述 | 默认值 |
|------|------|--------|
| `RDMA_INTERCEPT_ENABLE` | 启用拦截功能 | 1 |
| `RDMA_INTERCEPT_ENABLE_QP_CONTROL` | 启用QP控制 | 0 |
| `RDMA_INTERCEPT_ENABLE_MR_CONTROL` | 启用MR控制 | 0 |
| `RDMA_TENANT_ID` | 租户ID | 0 |
| `RDMA_INTERCEPT_MAX_QP_PER_PROCESS` | 每进程最大QP数 | 100 |
| `RDMA_INTERCEPT_MAX_MR_PER_PROCESS` | 每进程最大MR数 | 100 |
| `RDMA_INTERCEPT_LOG_LEVEL` | 日志级别 | INFO |
| `RDMA_INTERCEPT_LOG_FILE_PATH` | 日志文件路径 | /tmp/rdma_intercept.log |

### 租户管理命令

```bash
# 创建租户
sudo ./tenant_manager_client create <tenant_id> <max_qp> <max_mr> <max_memory> <name>

# 更新租户配额
sudo ./tenant_manager_client update <tenant_id> <max_qp> <max_mr> <max_memory>

# 删除租户
sudo ./tenant_manager_client delete <tenant_id>

# 查询租户
sudo ./tenant_manager_client query <tenant_id>

# 列出所有租户
sudo ./tenant_manager_client list
```

## 实验验证

项目包含9个核心实验，验证系统各项功能：

| 实验 | 目录 | 描述 |
|------|------|------|
| EXP-1 | `experiments/exp1_microbenchmark/` | 微基准测试，测量拦截开销 |
| EXP-2 | `experiments/exp2_multi_tenant_isolation/` | 多租户隔离测试 |
| EXP-3 | `experiments/exp3_scalability/` | 可扩展性测试 |
| EXP-4 | `experiments/exp4_cache/` | 缓存性能测试 |
| EXP-5 | `experiments/exp5_dynamic_policy/` | 动态策略热更新测试 |
| EXP-6 | `experiments/exp6_bandwidth_impact/` | 数据面带宽影响测试 |
| EXP-7 | `experiments/exp7_bandwidth_isolation/` | 带宽隔离测试 |
| EXP-8 | `experiments/exp8_qp_isolation/` | QP资源隔离测试 |
| EXP-9 | `experiments/exp9_mr_isolation/` | MR资源隔离测试 |
| EXP-MR | `experiments/exp_mr_dereg/` | MR注销攻击防护测试 |

每个实验目录包含详细的README.md，描述实验设置、方法、结果和结论。

## 技术原理

### 拦截流程

1. **应用调用RDMA API**（如`ibv_create_qp`）
2. **拦截库拦截调用**（通过LD_PRELOAD）
3. **检查租户配额**：
   - 从共享内存获取租户当前资源使用
   - 比较当前使用与配额限制
4. **决策**：
   - 符合配额 → 允许操作 → 执行原始API → 更新资源计数
   - 超出配额 → 拒绝操作 → 返回错误码

### 共享内存架构

```
共享内存布局:
┌─────────────────────────────────────────────────────────────┐
│                    租户管理共享内存                          │
├─────────────────────────────────────────────────────────────┤
│  头部信息 (tenant_shm_header_t)                              │
│  - 魔数、版本、租户数量                                       │
├─────────────────────────────────────────────────────────────┤
│  租户数组 (tenant_info_t[MAX_TENANTS])                       │
│  - 租户ID、名称、配额                                         │
│  - 当前QP数、MR数、内存使用                                   │
│  - 状态、创建时间                                             │
├─────────────────────────────────────────────────────────────┤
│  进程资源数组 (process_resource_t[MAX_PROCESSES])            │
│  - PID、租户ID、QP数、MR数、内存使用                          │
└─────────────────────────────────────────────────────────────┘
```

### 数据同步机制

- **读写锁**：保护共享内存的并发访问
- **原子操作**：资源计数更新
- **版本号**：检测数据更新
- **本地缓存**：减少共享内存访问频率

## 性能特性

### 开销分析

| 操作 | 延迟 | 说明 |
|------|------|------|
| 拦截检查 | ~500 ns | 含配额检查 |
| 共享内存访问 | ~200 ns | 本地缓存命中时 |
| 缓存未命中 | ~1000 ns | 访问共享内存 |
| 总体开销 | <1% | 相比原生RDMA |

### 性能优势

- **无内核依赖**：纯用户态实现，无需内核模块
- **低开销**：本地缓存减少共享内存访问
- **可扩展**：支持数百个租户并发
- **热更新**：动态调整配额无需重启

## 使用场景

### 多租户资源隔离
在云环境中为不同租户分配独立的RDMA资源配额，防止资源竞争，确保服务质量。

### 高性能计算集群
在HPC环境中实现节点内资源公平分配，避免单个应用耗尽所有RDMA资源。

### 生产环境管控
在生产环境中实施资源配额管理，防止恶意或故障应用占用过多RDMA资源。

### 性能测试与验证
在测试环境中模拟资源受限场景，验证应用的鲁棒性和资源管理策略的有效性。

## 限制与注意事项

### 系统要求
- Linux内核 ≥ 4.15
- RDMA硬件支持（Mellanox/NVIDIA等）
- POSIX共享内存支持
- libibverbs库

### 权限要求
- 租户管理守护进程需要root权限
- 应用程序需要访问共享内存权限
- 共享内存文件权限设置为0666

### 已知限制
- 带宽隔离需要硬件QoS支持
- 进程级MR限制当前依赖租户管理
- 需要确保共享内存名称唯一性

## 故障排除

### 常见问题

1. **拦截无效**
   - 确认LD_PRELOAD正确设置
   - 检查`RDMA_INTERCEPT_ENABLE=1`
   - 确认租户ID已设置

2. **配额未生效**
   - 检查租户管理守护进程是否运行
   - 确认租户已创建且配额正确
   - 检查`RDMA_INTERCEPT_ENABLE_QP_CONTROL=1`

3. **共享内存访问失败**
   - 确认共享内存正确初始化
   - 检查文件权限：`/dev/shm/rdma_intercept_shm`
   - 重启租户管理守护进程

### 调试信息

- 日志文件：`/tmp/rdma_intercept.log`
- 共享内存：`/dev/shm/rdma_intercept_shm`
- 租户管理日志：`/tmp/tenant_manager.log`

## 版本记录

### v2.0.0 (2026-04-14)

#### 主要更新
- **租户管理**：引入租户级资源隔离
- **动态策略**：支持运行时配额调整
- **性能优化**：本地缓存减少共享内存访问
- **实验完善**：新增9个核心实验

#### 架构调整
- 移除eBPF相关代码（当前使用LD_PRELOAD方案）
- 统一使用租户管理守护进程
- 优化共享内存布局

### v1.0.0 (2026-02-05)

#### 初始版本
- LD_PRELOAD拦截基础功能
- 共享内存通信机制
- 全局和每进程资源限制

## 贡献指南

欢迎提交Issue和Pull Request！

## 许可证

MIT License

## 联系方式

如有问题，请提交Issue或联系项目维护者。
