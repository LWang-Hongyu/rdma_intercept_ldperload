cmake_minimum_required(VERSION 3.10)
project(rdma_intercept_ldpreload VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -fPIC")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -DNDEBUG")

# 链接选项 - 防止未定义符号导致的问题
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")

# 查找依赖
find_package(Threads REQUIRED)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    /usr/include/infiniband
)

# 链接目录
link_directories(/usr/lib/x86_64-linux-gnu)

# 源文件
set(INTERCEPT_SOURCES
    src/intercept_core.c
    src/rdma_hooks.c
    src/logger.c
    src/config.c
    src/collector_client.c
    src/dynamic_policy.c
)

# 主拦截库
add_library(rdma_intercept SHARED ${INTERCEPT_SOURCES})
target_link_libraries(rdma_intercept 
    ibverbs
    rdmacm
    ${CMAKE_THREAD_LIBS_INIT}
    dl
)

# 数据收集服务
add_executable(collector_server src/collector_server.c)
target_link_libraries(collector_server 
    ${CMAKE_THREAD_LIBS_INIT}
)

# 测试程序（已移除，使用tests目录下的脚本）

# 单元测试（已移除，使用tests目录下的脚本）

# 安装规则
install(TARGETS rdma_intercept
    LIBRARY DESTINATION lib
)

install(FILES 
    include/rdma_intercept.h
    DESTINATION include
)

install(PROGRAMS
    scripts/run_with_intercept.sh
    DESTINATION bin
)