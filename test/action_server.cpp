#include "com_ipc.h"
#include <iostream>
#include <thread>

// 自定义目标、反馈、结果类型（注意：不包含goal_id，由框架提供）
struct MoveGoal {
    float x;
    float y;
};

struct MoveFeedback {
    float current_x;
    float current_y;
};

struct MoveResult {
    bool success;
    float final_x;
    float final_y;
};

// 服务器执行回调函数
void moveExecute(int goal_id, const void* goal_data, size_t size, ActionServer* server) {
    std::cout << "ActionServer: moveExecute called for goal " << goal_id << std::endl;
    if (size != sizeof(MoveGoal)) {
        std::cerr << "Error: goal size mismatch\n";
        return;
    }
    const MoveGoal* goal = static_cast<const MoveGoal*>(goal_data);
    std::cout << "Server: Starting execution of goal " << goal_id 
              << " to (" << goal->x << ", " << goal->y << ")\n";

    MoveFeedback fb;
    fb.current_x = 0;
    fb.current_y = 0;

    // 模拟长时间任务，分10步完成
    for (int i = 0; i < 10; ++i) {
        // 检查是否被取消
        if (server->isPreempted(goal_id)) {
            std::cout << "Server: Goal " << goal_id << " preempted\n";
            MoveResult res;
            res.success = false;
            res.final_x = fb.current_x;
            res.final_y = fb.current_y;
            server->publishResult(goal_id, res, ACTION_PREEMPTED);
            return;
        }

        // 更新反馈位置
        fb.current_x += goal->x / 10;
        fb.current_y += goal->y / 10;

        // 发布反馈
        if (!server->publishFeedback(goal_id, fb)) {
            std::cerr << "Server: Failed to publish feedback\n";
        } else {
            std::cout << "Server: Published feedback (" << fb.current_x << ", " << fb.current_y << ")\n";
        }

        // 模拟耗时操作
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 发布最终结果
    MoveResult res;
    res.success = true;
    res.final_x = goal->x;
    res.final_y = goal->y;
    if (!server->publishResult(goal_id, res, ACTION_SUCCEEDED)) {
        std::cerr << "Server: Failed to publish result\n";
    } else {
        std::cout << "Server: Published result, success\n";
    }
}

int main() {
    try {
        SystemManager::instance();  // 初始化IPC管理器

        // 创建动作服务器，动作名称为 "move_base"
        ActionServer server("move_base", moveExecute);
        server.start();  // 启动后台处理线程

        std::cout << "Action server started. Press Enter to stop.\n";
        std::cin.get();  // 等待用户输入后退出
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}