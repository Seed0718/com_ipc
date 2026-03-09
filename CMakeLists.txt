# 1. 声明要求的最低 CMake 版本
cmake_minimum_required(VERSION 3.10)

# 2. 声明项目名称和支持的语言
project(com_ipc_project LANGUAGES CXX)

# 3. 设置全局 C++ 标准为 C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ==========================================
# 规范输出目录
# ==========================================
# 强制将编译出的可执行文件放到 build/bin 目录下
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# 强制将编译出的动态链接库 (.so) 放到 build/lib 目录下
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 寻找 Linux 系统级的多线程库
find_package(Threads REQUIRED)

# 告诉编译器去 inc 文件夹里找头文件
include_directories(${CMAKE_SOURCE_DIR}/inc)

# ==========================================
# 核心底层库构建 (libcom_ipc.so)
# ==========================================
# 强制开启位置无关代码 (PIC)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 显式列出底层框架的所有核心源文件（加上了新增的 memory_pool.cpp）
set(IPC_SOURCES
    src/system_manager.cpp
    src/memory_pool.cpp
    src/publisher.cpp
    src/subscriber.cpp
    src/service_server.cpp
    src/service_client.cpp
    src/action_server.cpp
    src/action_client.cpp
)

# 生成共享库
add_library(com_ipc SHARED ${IPC_SOURCES})

# 链接系统的 pthread 和 rt (用于 shm_open 等共享内存操作)
target_link_libraries(com_ipc PUBLIC Threads::Threads rt)

# ==========================================
# 测试程序构建
# ==========================================
macro(add_test_executable TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} test/${SOURCE_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE com_ipc)
endmacro()

# 批量生成测试节点
add_test_executable(publisher publisher.cpp)
add_test_executable(subscriber subscriber.cpp)
add_test_executable(service_server service_server.cpp)
add_test_executable(service_client service_client.cpp)
add_test_executable(action_server action_server.cpp)
add_test_executable(action_client action_client.cpp)
add_test_executable(pub_zerocopy pub_zerocopy.cpp)
add_test_executable(sub_zerocopy sub_zerocopy.cpp)