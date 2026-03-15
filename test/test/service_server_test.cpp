#include "com_ipc.h"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

using namespace com_ipc;

// 必须与客户端保持完全一致的数据结构体
struct AddTwoIntsRequest {
    int a;
    int b;
};

struct AddTwoIntsResponse {
    int sum;
};

// 【新增】：定义一个原子标志位，用于通知主线程任务已完成
std::atomic<bool> g_task_completed{false};

// 服务端回调函数：处理业务逻辑
bool handleAddTwoInts(const AddTwoIntsRequest& req, AddTwoIntsResponse& res) {
    std::cout << "[Service Server] 收到计算请求: " << req.a << " + " << req.b << std::endl;
    
    // 执行计算
    res.sum = req.a + req.b;
    
    std::cout << "[Service Server] 计算完成，即将返回结果: " << res.sum << std::endl;
    std::cout << "-----------------------------------" << std::endl;
    
    // 标记任务已完成，准备通知主线程退出
    g_task_completed = true; 
    
    return true; // 返回 true 表示处理成功，底层框架紧接着会将 res 发回给客户端
}

int main() {
    // 初始化系统大管家
    SystemManager::instance();

    std::cout << "[Service Server] 服务端启动，准备提供单次计算服务..." << std::endl;

    // 创建服务提供者
    ServiceServer<AddTwoIntsRequest, AddTwoIntsResponse> server(
        "add_two_ints", 
        handleAddTwoInts
    );

    // 启动后台异步监听
    server.startAsync();

    // 轮询检查标志位，如果任务还没完成，就休眠 50ms 继续等
    while (!g_task_completed && !g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (g_task_completed) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "[Service Server] 响应已发送，服务端完成使命，自动退出。" << std::endl;
    }

    SystemManager::destroy();
    return 0;
}