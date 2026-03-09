cmake_minimum_required(VERSION 3.10)
project(com_ipc VERSION 1.0.0)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ==========================================
# 1. 全局编译设置
# ==========================================
# 设置 C++ 标准为 C++11，并强制要求
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库：多线程和 Linux 实时库 (共享内存 API 需要 rt)
find_package(Threads REQUIRED)

# 设置全局头文件搜索路径
include_directories(inc)

# ==========================================
# 2. 核心底层通信库 (Core Library)
# ==========================================
# 收集 src 目录下的所有核心源码 
# (结合你之前的编译日志，我把你写过的 service 和 action 也加进来了，防止报错)
set(IPC_SOURCES
    src/memory_pool.cpp
    src/system_manager.cpp
    src/publisher.cpp
    src/subscriber.cpp
    src/node.cpp
    src/service_server.cpp
    src/service_client.cpp
    src/action_server.cpp
    src/action_client.cpp
)

# 将核心源码编译为共享动态库 libcom_ipc.so，方便后续各种工具和 Python 链接
add_library(com_ipc SHARED ${IPC_SOURCES})
target_link_libraries(com_ipc PUBLIC Threads::Threads rt)

# ==========================================
# 3. Python 绑定 (pybind11) -> 兼容 Ubuntu 18.04 / CMake 3.10
# ==========================================
# 1. 使用老版本的语法寻找 Python3 解释器
find_package(PythonInterp 3 REQUIRED)

# 2. 让 Python 解释器自己吐出 pip 安装的 pybind11 的 CMake 路径
# 注意：老版本中 Python 解释器的变量名是 PYTHON_EXECUTABLE (全大写)
execute_process(
    COMMAND ${PYTHON_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# 3. 带着正确的路径，瞬间找到最新版的 pybind11！
find_package(pybind11 REQUIRED)

# 4. 编译 Python 动态库
pybind11_add_module(com_ipc_py src/pybind_wrapper.cpp)
target_link_libraries(com_ipc_py PRIVATE com_ipc)
# ==========================================
# 4. 基础测试用例 (Tests)
# ==========================================
# 定义一个宏，用来快速编译 test/ 目录下的测试程序
macro(add_test_executable name source_file)
    add_executable(${name} test/${source_file})
    target_link_libraries(${name} PRIVATE com_ipc)
endmacro()

# 编译你的测试程序 (确保 test/ 目录下有这些文件)
add_test_executable(pub_zerocopy pub_zerocopy.cpp)
add_test_executable(sub_zerocopy sub_zerocopy.cpp)
add_test_executable(multi_node_test multi_node_test.cpp)

# ==========================================
# 5. 命令行系统工具链 (Tools)
# ==========================================
# 探针工具 (Topic Monitor)
add_executable(com_topic tools/com_topic.cpp)
target_link_libraries(com_topic PRIVATE com_ipc)

# 节点管理器 (Node Manager)
add_executable(com_node tools/com_node.cpp)
target_link_libraries(com_node PRIVATE com_ipc)

# 数据录制回放工具 (Bag Record & Play)
add_executable(com_bag tools/com_bag.cpp)
target_link_libraries(com_bag PRIVATE com_ipc)

# 跨机网络通信网关 (Distributed Router)
add_executable(com_router tools/com_router.cpp)
target_link_libraries(com_router PRIVATE com_ipc)