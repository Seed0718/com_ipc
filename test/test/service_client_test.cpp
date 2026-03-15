#include "com_ipc.h"
#include <iostream>
#include <thread> // 需要包含 thread
#include <chrono> // 需要包含 chrono

using namespace com_ipc;

// 必须与服务端保持完全一致的数据结构体
struct AddTwoIntsRequest {
    int a;
    int b;
};

struct AddTwoIntsResponse {
    int sum;
};

int main() {
    SystemManager::instance();

    ServiceClient<AddTwoIntsRequest, AddTwoIntsResponse> client("add_two_ints");

    std::cout << "[Service Client] 客户端启动，发送单次计算请求..." << std::endl;

    // 【核心修复】：等待底层共享内存和订阅者状态完全建立
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); 

    AddTwoIntsRequest req;
    req.a = 66;
    req.b = 99;
    
    AddTwoIntsResponse res;

    std::cout << "[Service Client] 发起请求: " << req.a << " + " << req.b << " = ?" << std::endl;

    if (client.call(req, res)) {
        std::cout << "[Service Client] 调用成功! 服务端返回结果: " << res.sum << std::endl;
    } else {
        std::cerr << "[Service Client] 调用失败或超时！(请确认服务端已启动)" << std::endl;
    }
    
    std::cout << "===================================" << std::endl;
    std::cout << "[Service Client] 演示完毕，客户端正常退出。" << std::endl;

    SystemManager::destroy();
    return 0;
}