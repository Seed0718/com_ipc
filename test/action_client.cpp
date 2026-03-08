#include "com_ipc.h"
#include <iostream>
#include <thread>

// 必须与服务器定义完全一致
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

int main() {
    try {
        SystemManager::instance();

        // 创建动作客户端，连接到 "move_base"
        ActionClient client("move_base");

        // 构造目标
        MoveGoal goal;
        goal.x = 10.0f;
        goal.y = 20.0f;

        // 发送目标
        int goal_id = client.sendGoal(goal);
        if (goal_id < 0) {
            std::cerr << "Failed to send goal\n";
            return 1;
        }
        std::cout << "Client: Sent goal " << goal_id << "\n";

        // 循环等待结果，同时获取反馈
        while (!client.waitForResult(goal_id, 500)) {  // 每500ms检查一次
            MoveFeedback fb;
            ActionStatus status;
            if (client.getFeedback(goal_id, fb, status)) {
                std::cout << "Client: Received feedback (" << fb.current_x << ", " << fb.current_y << ")\n";
            }
        }

        // 获取最终结果
        MoveResult res;
        ActionStatus final_status;
        if (client.getResult(goal_id, res, final_status)) {
            std::cout << "Client: Received result: success=" << res.success
                      << ", final=(" << res.final_x << ", " << res.final_y
                      << "), status=" << final_status << "\n";
        } else {
            std::cout << "Client: Failed to get result\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}