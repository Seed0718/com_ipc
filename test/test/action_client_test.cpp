#include "com_ipc.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace com_ipc;

// 数据结构必须与服务端完全一致
struct NavGoal { float target_x; float target_y; };
struct NavFeedback { float progress; float distance_remaining; };
struct NavResult { bool success; int time_cost_ms; };

int main() {
    SystemManager::instance();

    // 1. 创建 Action Client
    ActionClient<NavGoal, NavFeedback, NavResult> client("navigate");
    
    std::cout << "[Action Client] 客户端启动，准备发送导航任务..." << std::endl;
    // 休眠一小会儿等底层共享内存通道完全建好（吸取上一次的教训）
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 2. 构造并发送 Goal
    NavGoal goal{15.0f, 30.5f};
    int goal_id = client.sendGoal(goal);

    if (goal_id < 0) {
        std::cerr << "[Action Client] 发送目标失败！" << std::endl;
        SystemManager::destroy();
        return -1;
    }

    std::cout << "[Action Client] 成功发送目标，获得内部追踪 ID: " << goal_id << std::endl;
    std::cout << "[Action Client] 正在监听执行进度..." << std::endl;

    // 3. 非阻塞轮询：一边喝茶，一边看进度
    while (!g_shutdown_requested) {
        // 如果任务做完了，跳出循环去拿最终结果
        if (client.isFinished(goal_id)) {
            break; 
        }

        // 尝试获取最新进度 Feedback
        NavFeedback fb;
        ActionStatus status;
        if (client.getFeedback(goal_id, fb, status)) {
            std::cout << "  -> [Feedback] 收到实时进度: " << fb.progress 
                      << "%, 距离终点还剩: " << fb.distance_remaining << " m" << std::endl;
        }

        // 没必要死循环狂刷，休息 200ms 再问
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 4. 任务结束，提取最终 Result
    NavResult res;
    ActionStatus status;
    if (client.getResult(goal_id, res, status)) {
        std::cout << "\n[Action Client] 任务结束！底层状态码: " << status << std::endl;
        std::cout << "  -> [Result] 成功到达: " << (res.success ? "是" : "否") 
                  << " | 历时: " << res.time_cost_ms << " 毫秒" << std::endl;
    } else {
        std::cerr << "[Action Client] 任务异常中断或获取结果失败。" << std::endl;
    }

    SystemManager::destroy();
    return 0;
}