#include <iostream>
#include <thread>
#include <chrono>
#include "com_ipc/api/action_server.h"
#include "com_ipc/api/action_client.h"

using namespace com_ipc;

// 自定义强类型结构体
struct NavGoal { float target_x; float target_y; };
struct NavFeedback { float distance_remaining; };
struct NavResult { bool success; int time_cost; };

int main() {
    std::cout << "--- Starting Action Test ---" << std::endl;

    // 1. 将 Server 放入独立的后台线程运行（模拟独立的节点进程）
    std::thread server_thread([]() {
        std::cout << "[Server Process] 正在启动并监听..." << std::endl;
        ActionServer<NavGoal, NavFeedback, NavResult> server("navigate", 
            [](int goal_id, const NavGoal& goal, ActionServer<NavGoal, NavFeedback, NavResult>* srv) {
                std::cout << "[Server] 收到导航目标: x=" << goal.target_x << ", y=" << goal.target_y << std::endl;
                
                // 模拟耗时 2.5 秒的导航过程，每 0.5 秒发一次反馈
                for (int i = 5; i > 0; i--) {
                    srv->publishFeedback(goal_id, NavFeedback{static_cast<float>(i * 1.5)});
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                
                // 发送最终结果
                srv->publishResult(goal_id, NavResult{true, 2500}, ACTION_SUCCEEDED);
                std::cout << "[Server] 导航任务完成！" << std::endl;
            }
        );
        server.start();
        
        // 让 Server 存活 5 秒钟以处理任务
        std::this_thread::sleep_for(std::chrono::seconds(5));
        server.shutdown();
        std::cout << "[Server Process] 退出。" << std::endl;
    });

    // 主线程稍微等 100 毫秒，确保 Server 已经进入阻塞等待状态
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 2. 在主线程启动 Client
    std::cout << "[Client Process] 正在启动..." << std::endl;
    ActionClient<NavGoal, NavFeedback, NavResult> client("navigate");
    
    // 发送目标
    int goal_id = client.sendGoal(NavGoal{100.5f, -20.0f});
    std::cout << "[Client] 已发送目标，Goal ID: " << goal_id << std::endl;

    // 接收实时反馈
    while (!client.isFinished(goal_id)) {
        NavFeedback fb;
        ActionStatus status;
        if (client.getFeedback(goal_id, fb, status)) {
            std::cout << "[Client] 收到实时反馈: 剩余距离=" << fb.distance_remaining << "m" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    // 接收最终结果
    NavResult result;
    ActionStatus status;
    if (client.getResult(goal_id, result, status)) {
        std::cout << "[Client] 收到最终结果: 成功=" << result.success << ", 耗时=" << result.time_cost << "ms" << std::endl;
    }

    // 等待 Server 线程结束
    server_thread.join();
    std::cout << "--- Action Test Finished ---" << std::endl;
    return 0;
}