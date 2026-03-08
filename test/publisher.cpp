#include "com_ipc.h"
#include <iostream>
#include <thread>
#include <cmath>
#include <csignal>
#include <cstdlib>

// 自定义消息结构
struct RobotState {
    double x, y, theta;
    double linear_vel, angular_vel;
    int sensor[10];
};

static volatile bool running = true;

void signal_handler(int) {
    running = false;
    g_shutdown_requested = 1;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <topic_name>\n";
        return 1;
    }

    signal(SIGINT, signal_handler);

    try {
        SystemManager::instance();  // 初始化

        Publisher pub(argv[1]);

        RobotState state{};
        int seq = 0;

        while (running) {
            // 模拟运动
            state.x += 0.1 * std::cos(state.theta);
            state.y += 0.1 * std::sin(state.theta);
            state.theta += 0.05;
            state.linear_vel = 0.1;
            state.angular_vel = 0.05;
            for (int i = 0; i < 10; ++i) {
                state.sensor[i] = std::rand() % 100;
            }

            if (pub.publish(state)) {
                std::cout << "Published #" << seq++
                          << " at (" << state.x << ", " << state.y << ")\n";
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (seq % 5 == 0) {
                SystemManager::instance()->listTopics();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    SystemManager::destroy();
    return 0;
}