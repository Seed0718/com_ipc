#include "com_ipc.h"
#include <iostream>
#include <csignal>

static volatile bool running = true;

void signal_handler(int) {
    running = false;
    g_shutdown_requested = 1;
}

struct RobotState {
    double x, y, theta;
    double linear_vel, angular_vel;
    int sensor[10];
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <topic_name>\n";
        return 1;
    }

    signal(SIGINT, signal_handler);

    try {
        SystemManager::instance();

        Subscriber sub(argv[1]);

        SystemManager::instance()->listTopics();

        RobotState state;
        while (running) {
            if (sub.receive(state, 1000)) {  // 1秒超时
                std::cout << "Received: x=" << state.x
                          << ", y=" << state.y
                          << ", theta=" << state.theta
                          << ", seq=" << sub.getLastSeq()    // 可从消息头获取，这里简化
                          << '\n';
            } else {
                // 超时或错误
                // std::cout << "No message\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    SystemManager::destroy();
    return 0;
}