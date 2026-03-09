#include "com_ipc.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

void signal_handler(int) {
    g_shutdown_requested = 1;
}

int main() {
    signal(SIGINT, signal_handler);
    SystemManager::instance();

    // 1. 在同一个进程里，创建两个节点！
    Node camera_node("CameraNode");
    Node lidar_node("LidarNode");

    // 2. Lidar 节点订阅相机数据 (使用极简的 Lambda 表达式回调)
    lidar_node.createSubscriber("image_topic", [](const Subscriber::LoanedMessage& msg) {
        std::cout << "[LidarNode] Received image. Size: " << msg.size 
                  << " bytes | Seq: " << msg.seq << "\n";
    });

    // 3. Camera 节点负责发布数据（新开个普通线程去发，模拟真实业务）
    Publisher* pub = camera_node.createPublisher("image_topic");
    std::thread camera_thread([pub]() {
        int seq = 0;
        while (!g_shutdown_requested) {
            // 零拷贝发个 10KB 假数据
            void* ptr = pub->loanRaw(10240); 
            pub->publishLoaned(ptr, 10240);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    // 4. 万事俱备，让主线程接管宇宙！
    SystemManager::spin();

    if (camera_thread.joinable()) camera_thread.join();
    SystemManager::destroy();
    return 0;
}