// #include "com_ipc.h"
// #include <iostream>
// #include <thread>
// #include <chrono>

// using namespace com_ipc;

// // 自定义数据结构体 
// // 底层使用共享内存，结构体最好是平凡数据类型，不要包含 std::string 等带指针的容器)
// struct SensorData {
//     int sensor_id;
//     float temperature;
//     double position[3];
// };

// int main() {
//     // 1. 初始化系统资源大管家
//     SystemManager::instance();

//     // 2. 注册当前节点
//     Node node("SensorPublisherNode");

//     // 3. 创建强类型的发布者，话题名为 "sensor_data_topic"
//     Publisher<SensorData>* pub = node.createPublisher<SensorData>("sensor_data_topic");

//     std::cout << "[Publisher] 节点启动，开始发布数据..." << std::endl;

//     int count = 0;
//     while (!g_shutdown_requested) { // 捕获 Ctrl+C 退出信号
//         // 构造要发送的数据
//         SensorData data;
//         data.sensor_id = 1001;
//         data.temperature = 25.5f + (count * 0.1f); // 模拟温度变化
//         data.position[0] = 1.0 + count;
//         data.position[1] = 2.0 + count;
//         data.position[2] = 3.0 + count;

//         // 发布数据 (这里走的是普通拷贝发布。如果数据极大，可以使用 pub->loan() 走零拷贝)
//         if (pub->publish(data)) {
//             std::cout << "[Publisher] 成功发布第 " << count << " 条数据 | 温度: " 
//                       << data.temperature << std::endl;
//         } else {
//             std::cerr << "[Publisher] 发布失败！" << std::endl;
//         }

//         count++;
//         // 模拟 1Hz 的发送频率
//         std::this_thread::sleep_for(std::chrono::milliseconds(1000));
//     }

//     // 4. 清理系统资源
//     SystemManager::destroy();
//     return 0;
// }

// #include "com_ipc/api/publisher.h"
// #include <iostream>
// #include <string>
// #include <thread>

// int main() {
//     std::cout << "=== Navigation Node (Publisher) Started ===\n";
    
//     com_ipc::Publisher<std::string> pub("cmd_vel");

//     int count = 0;
//     while (true) {
//         std::string msg = "v_x = 1.0 m/s, seq = " + std::to_string(++count);
//         pub.publish(msg);
//         std::cout << "[导航输出] 发布: " << msg << std::endl;
        
//         // 模拟 10Hz 的正常控制频率 (100ms 发一次)
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     }

//     return 0;
// }


#include "com_ipc/api/publisher.h"
#include <iostream>
#include <thread>

// 1. 保持与接收端相同的结构体定义
struct VelocityCmd {
    float v_x;
    float v_y;
    uint32_t seq;
};

int main() {
    std::cout << "=== Navigation Node (Publisher) Started ===\n";
    
    // 2. 发布类型也改为结构体
    com_ipc::Publisher<VelocityCmd> pub("cmd_vel");

    uint32_t count = 0;
    while (true) {
        VelocityCmd cmd;
        cmd.v_x = 1.0f;
        cmd.v_y = 0.0f;
        cmd.seq = ++count;

        // 直接发布结构体，底层会自动计算 sizeof(VelocityCmd) 并写入共享内存
        pub.publish(cmd);
        std::cout << "[导航输出] 发布: v_x=" << cmd.v_x 
                  << " m/s, seq=" << cmd.seq << std::endl;
        
        // 模拟 10Hz 的正常控制频率
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}