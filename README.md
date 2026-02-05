<!-- created at: 2026-02-05 11:58:00 -->
# RDMA资源隔离与控制拦截系统

基于eBPF的RDMA资源隔离与控制拦截系统，通过LD_PRELOAD技术拦截RDMA操作，实现细粒度的资源配额管理和性能隔离。

## 项目概述

本项目旨在解决多租户环境下RDMA资源竞争问题，通过以下技术手段实现资源隔离：

- **eBPF监控**：利用kprobe监控内核RDMA操作
- **LD_PRELOAD拦截**：动态库注入实现用户态拦截
- **集中式管控**：collector_server统一管理资源配额
- **实时控制**：基于eBPF数据的实时资源限制

## 系统架构

```
┌─────────────────┐    ┌──────────────────┐    ┌──────────────────┐
│   Application │───▶│ librdma_intercept│───▶│ collector_server │
│                 │    │     (Intercept)  │    │   (Control)      │
└─────────────────┘    └──────────────────┘    └──────────────────┘
                              │                          │
                              ▼                          ▼
                       ┌──────────────────┐    ┌──────────────────┐
                       │   eBPF Program │◀───▶│   eBPF Maps      │
                       │    (Monitor)     │    │ (Resource Data)  │
                       └──────────────────┘    └──────────────────┘
```

### 核心组件

#### 1. eBPF监控程序 (`rdma_monitor`)
- 通过kprobe监控RDMA核心函数调用
- 跟踪QP创建/销毁、MR注册/注销等操作
- 实时更新资源使用统计

#### 2. 动态拦截库 (`librdma_intercept.so`)
- LD_PRELOAD注入用户程序
- 拦截RDMA API调用
- 查询资源配额并执行访问控制

#### 3. 数据收集服务 (`collector_server`)
- 从eBPF maps读取资源数据
- 提供Unix域套接字接口
- 实施全局资源限制策略

## 功能特性

### 资源限制
- **每进程QP限制**：控制单个进程的QP数量
- **全局QP限制**：控制整个系统的QP总数
- **内存区域限制**：限制MR数量和内存使用
- **工作请求限制**：控制Send/Recv WR数量

### 监控能力
- **实时监控**：基于eBPF的零开销监控
- **细粒度统计**：按进程维度统计资源使用
- **全局视图**：系统级资源使用概览

### 配置管理
- **环境变量配置**：灵活的运行时配置
- **动态策略更新**：支持运行时策略调整
- **日志记录**：详细的拦截和监控日志

## 快速开始

### 环境准备
```bash
# 确保系统支持eBPF
sudo apt install -y libbpf-dev libelf-dev zlib1g-dev
sudo apt install -y ibverbs-utils rdmacm-utils
```

### 构建项目
```bash
cd /path/to/rdma_intercept_ldpreload
chmod +x build_all.sh
./build_all.sh
```

### 运行示例

1. **启动eBPF监控**：
```bash
cd build
sudo ./rdma_monitor
```

2. **启动数据收集服务**：
```bash
export RDMA_INTERCEPT_MAX_GLOBAL_QP=100
sudo -E ./collector_server
```

3. **运行受保护的应用**：
```bash
export RDMA_INTERCEPT_ENABLE=1
export RDMA_INTERCEPT_ENABLE_QP_CONTROL=1
export RDMA_INTERCEPT_MAX_QP_PER_PROCESS=10
export RDMA_INTERCEPT_MAX_GLOBAL_QP=50
LD_PRELOAD=./librdma_intercept.so your_rdma_application
```

## 配置参数

### 环境变量
| 参数 | 描述 | 默认值 |
|------|------|--------|
| `RDMA_INTERCEPT_ENABLE` | 启用拦截功能 | 1 |
| `RDMA_INTERCEPT_ENABLE_QP_CONTROL` | 启用QP控制 | 0 |
| `RDMA_INTERCEPT_MAX_QP_PER_PROCESS` | 每进程最大QP数 | 100 |
| `RDMA_INTERCEPT_MAX_GLOBAL_QP` | 全局最大QP数 | 1000 |
| `RDMA_INTERCEPT_MAX_MR_PER_PROCESS` | 每进程最大MR数 | 1000 |
| `RDMA_INTERCEPT_MAX_MEMORY_PER_PROCESS` | 每进程最大内存(MB) | 10240 |
| `RDMA_INTERCEPT_LOG_LEVEL` | 日志级别 | INFO |
| `RDMA_INTERCEPT_LOG_FILE_PATH` | 日志文件路径 | /tmp/rdma_intercept.log |

### 支持的QP类型
- RC (Reliable Connection)
- UC (Unreliable Connection) 
- UD (Unreliable Datagram)
- RQ (Reliable Queue)

## 使用场景

### 多租户资源隔离
在云环境中为不同租户分配独立的RDMA资源配额，防止资源竞争。

### 性能测试
在测试环境中模拟资源受限场景，验证应用的鲁棒性。

### 生产环境管控
在生产环境中实施资源配额管理，确保服务质量。

## 技术原理

### eBPF监控机制
系统使用kprobe技术监控以下内核函数：
- `ib_uverbs_create_qp`: QP创建
- `ib_uverbs_destroy_qp`: QP销毁
- `ib_uverbs_reg_mr`: MR注册
- `ib_uverbs_dereg_mr`: MR注销

### 拦截流程
1. 应用调用RDMA API
2. 拦截库获取当前资源使用情况
3. 检查资源配额限制
4. 决定是否允许操作执行
5. 更新资源使用统计

### 数据同步
- eBPF程序实时更新maps中的资源计数
- collector_server定期读取maps数据
- 拦截库通过Unix套接字查询实时数据

## 性能特性

### 开销分析
- **eBPF监控开销**：微秒级，几乎无感知
- **拦截库开销**：每次API调用增加少量延迟
- **总体开销**：<5%的性能影响

### 优势
- 基于内核的高效监控
- 零拷贝的数据传输
- 异步的资源查询机制

## 限制与注意事项

### 系统要求
- Linux内核 ≥ 4.15
- eBPF功能已启用
- RDMA硬件支持

### 权限要求
- eBPF程序需要CAP_SYS_ADMIN权限
- collector_server需要root权限运行

### 已知限制
- 某些旧版内核可能不支持特定kprobe
- 需要手动配置Unix域套接字路径

## 故障排除

### 常见问题
1. **eBPF加载失败**：检查内核版本和权限
2. **拦截无效**：确认LD_PRELOAD正确设置
3. **资源统计不准**：检查eBPF监控程序是否运行

### 调试信息
- 日志文件：`/tmp/rdma_intercept.log`
- eBPF maps：`/sys/fs/bpf/*`
- Unix套接字：`/tmp/rdma_collector.sock`

## 测试验证

项目包含完整的测试套件：
- `test_intercept_functionality.sh`: 功能测试
- `test_global_limit.sh`: 全局限制测试
- `test_system_validation.sh`: 系统验证

## 贡献指南

欢迎提交Issue和Pull Request来改进项目。

## 许可证

本项目采用MIT许可证。