#include "com_ipc.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic> // 引入原子变量

using namespace com_ipc;

// 定义 Goal (目标)、Feedback (反馈)、Result (结果) 的数据结构
struct NavGoal {
    float target_x;
    float target_y;
};

struct NavFeedback {
    float progress;           
    float distance_remaining; 
};

struct NavResult {
    bool success;
    int time_cost_ms;         
};

// 【新增】：定义一个原子标志位，用于通知主线程任务已完成或被取消
std::atomic<bool> g_action_completed{false};

// 服务端执行回调函数 (在独立的后台线程中运行)
void execute_nav(int goal_id, const NavGoal& goal, ActionServer<NavGoal, NavFeedback, NavResult>* server) {
    std::cout << "\n[Action Server] 开始执行导航任务 Goal ID: " << goal_id 
              << " | 目标: (" << goal.target_x << ", " << goal.target_y << ")" << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    int total_steps = 5;

    // 模拟一段漫长的运动过程
    for (int i = 1; i <= total_steps; ++i) {
        // 检查客户端是否按下了“取消”按钮
        if (server->isPreempted(goal_id)) {
            std::cout << "[Action Server] 任务被客户端强制取消！" << std::endl;
            NavResult res{false, 0};
            server->publishResult(goal_id, res, ACTION_PREEMPTED); // 发送中止结果
            
            g_action_completed = true; // 即使被取消，当前服务周期也算结束了
            return;
        }

        // 模拟每次耗时 1 秒的运动
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // 构造并发送 Feedback 进度
        NavFeedback fb;
        fb.progress = (i * 100.0f) / total_steps;
        fb.distance_remaining = 10.0f - (i * 2.0f); 
        
        server->publishFeedback(goal_id, fb);
        std::cout << "[Action Server] 广播进度 Feedback: " << fb.progress << "%" << std::endl;
    }

    // 运动结束，计算耗时
    auto end_time = std::chrono::steady_clock::now();
    int cost = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // 构造并发送最终 Result
    NavResult res;
    res.success = true;
    res.time_cost_ms = cost;

    server->publishResult(goal_id, res, ACTION_SUCCEEDED);
    std::cout << "[Action Server] 导航任务完成！已发送最终 Result。" << std::endl;
    std::cout << "-----------------------------------" << std::endl;

    // 【新增】：标记任务彻底结束，通知 main 函数可以下班了
    g_action_completed = true;
}

int main() {
    SystemManager::instance();
    std::cout << "[Action Server] 动作服务端已启动，等待单次导航目标..." << std::endl;

    // 创建 Action Server 并绑定执行逻辑
    ActionServer<NavGoal, NavFeedback, NavResult> server("navigate", execute_nav);
    server.start();

    // 【修改】：不再死等，改为轮询检查标志位
    while (!g_action_completed && !g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (g_action_completed) {
        // 【关键防御】：多等 200 毫秒！给底层时间将 Result 写入共享内存。
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "[Action Server] 状态已同步，服务端完成单次使命，自动退出。" << std::endl;
    }
    
    server.shutdown();
    SystemManager::destroy();
    return 0;
}