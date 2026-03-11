#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include "com_ipc/api/node.h"

using namespace com_ipc;

int main() {
    std::cout << "--- Starting Modern Event-Loop Test ---" << std::endl;

    // 1. 极其清爽的节点创建
    Node camera_node("CameraNode");
    Node lidar_node("LidarNode");

    // ================= 修改这里的顺序 =================
    // 2. 先让 Camera 节点创建发布者 (这会在 /dev/shm 里真正开辟出物理内存)
    auto pub = camera_node.createPublisher<std::string>("image_topic");

    // 3. 再让 Lidar 节点创建订阅者 (瞬间挂载成功，绝不卡顿)
    lidar_node.createSubscriber<std::string>("image_topic", [](const std::string* msg) {
        std::cout << "[LidarNode Callback] 瞬间收到零拷贝数据！大小: " 
                  << msg->size() / 1024 << " KB" << std::endl;
    });

    // 4. 模拟硬件定时器 (比如相机以 2Hz 的频率产生 1MB 的图像帧)
    std::thread hw_timer_thread([&]() {
        int frame_count = 0;
        // 等待 500ms 确保对面的 Subscriber 已经建好
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
        
        // while (!g_shutdown_requested && frame_count < 5) {
        while (!g_shutdown_requested) {
            frame_count++;
            std::string image_data(1024 * 1024, 'X'); // 模拟生成 1MB 的巨大数据
            
            std::cout << "\n[CameraNode Hardware] 产生第 " << frame_count << " 帧图像并发布..." << std::endl;
            pub->publish(image_data); // 一键发布，底层自动放入内存池
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        g_shutdown_requested = true; // 发完5帧后通知全系统退出
    });

    // 5. 【核心高能预警】启动 Lidar 节点的单线程 Event Loop！
    // 这一句彻底取代了原来傻乎乎的 SystemManager::spin()
    // 它会以接近 0% 的 CPU 占用率，在单线程里飞速轮询所有挂载的 Subscriber
    std::cout << ">>> 启动 LidarNode 的事件循环调度器 <<<" << std::endl;
    lidar_node.spin(); 

    // 收尾工作
    if (hw_timer_thread.joinable()) {
        hw_timer_thread.join();
    }
    
    std::cout << "--- Modern Event-Loop Test Finished ---" << std::endl;
    return 0;
}