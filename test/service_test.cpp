#include <iostream>
#include <thread>
#include <chrono>
#include "com_ipc/api/service_server.h"
#include "com_ipc/api/service_client.h"

using namespace com_ipc;

// 1. 自定义强类型的请求与响应结构体
struct IkRequest {
    float target_x;
    float target_y;
    float target_theta;
};

struct IkResponse {
    float wheel_left_vel;
    float wheel_right_vel;
    bool reachable;
};

int main() {
    std::cout << "--- Starting Service Test ---" << std::endl;

    // 2. 将 Server 放入独立的后台线程运行（模拟运动学解算节点）
    std::thread server_thread([]() {
        std::cout << "[Server Process] 运动学服务 (IK Service) 正在启动..." << std::endl;
        
        // 实例化强类型 ServiceServer
        ServiceServer<IkRequest, IkResponse> server("compute_ik", 
            [](const IkRequest& req, IkResponse& resp) -> bool {
                std::cout << "[Server] 收到逆解请求: x=" << req.target_x 
                          << ", y=" << req.target_y 
                          << ", theta=" << req.target_theta << std::endl;
                
                // 模拟耗时的计算过程
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                
                // 填充响应数据
                if (req.target_x > 1000.0f) {
                    resp.reachable = false; // 超出工作空间
                    resp.wheel_left_vel = 0.0f;
                    resp.wheel_right_vel = 0.0f;
                } else {
                    resp.reachable = true;
                    // 随便模拟一个计算结果
                    resp.wheel_left_vel = req.target_x * 0.5f - req.target_theta;
                    resp.wheel_right_vel = req.target_x * 0.5f + req.target_theta;
                }
                
                std::cout << "[Server] 计算完成，即将返回结果。" << std::endl;
                return true; // 返回 true 表示成功处理并发送响应
            }
        );
        
        server.startAsync(); // 启动后台监听
        
        // 让 Server 存活 3 秒钟
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::cout << "[Server Process] 退出。" << std::endl;
    });

    // 主线程稍微等 100 毫秒，确保 Server 已经建好底层 Topic
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 3. 在主线程启动 Client（模拟行为决策节点）
    std::cout << "[Client Process] 决策节点正在启动..." << std::endl;
    ServiceClient<IkRequest, IkResponse> client("compute_ik");
    
    // 准备请求包
    IkRequest req = {150.0f, 20.0f, 3.14f};
    IkResponse resp;

    std::cout << "[Client] 正在发起 RPC 调用，等待响应..." << std::endl;
    
    // 发起同步调用 (设置 2000ms 超时)
    auto start_time = std::chrono::high_resolution_clock::now();
    bool success = client.call(req, resp, 2000);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto cost_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    if (success) {
        std::cout << "[Client] 调用成功！耗时: " << cost_time << " ms" << std::endl;
        std::cout << "[Client] 收到解算结果: 可达=" << (resp.reachable ? "Yes" : "No") 
                  << ", 左轮速=" << resp.wheel_left_vel 
                  << ", 右轮速=" << resp.wheel_right_vel << std::endl;
    } else {
        std::cout << "[Client] RPC 调用失败或超时！" << std::endl;
    }

    // 等待 Server 线程结束
    server_thread.join();
    std::cout << "--- Service Test Finished ---" << std::endl;
    return 0;
}