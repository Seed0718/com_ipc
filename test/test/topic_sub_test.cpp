// #include "com_ipc.h"
// #include <iostream>

// using namespace com_ipc;

// // 必须与发布者保持一致的数据结构体
// struct SensorData {
//     int sensor_id;
//     float temperature;
//     double position[3];
// };

// // 强类型回调函数：当有新数据到达时，底层会触发此函数
// void sensorDataCallback(const SensorData* msg) {
//     if (msg == nullptr) return;
    
//     std::cout << ">>> [Subscriber] 接收到新数据:" << std::endl;
//     std::cout << "    ID: " << msg->sensor_id << std::endl;
//     std::cout << "    温度: " << msg->temperature << " °C" << std::endl;
//     std::cout << "    位置: (" << msg->position[0] << ", " 
//                              << msg->position[1] << ", " 
//                              << msg->position[2] << ")" << std::endl;
//     std::cout << "-----------------------------------" << std::endl;
// }

// int main() {
//     // 1. 初始化系统资源
//     SystemManager::instance();

//     // 2. 注册当前节点
//     Node node("AlgorithmSubscriberNode");

//     // 3. 创建订阅者，并绑定回调函数 (底层会将这个任务挂载到异步事件队列中)
//     Subscriber<SensorData>* sub = node.createSubscriber<SensorData>(
//         "sensor_data_topic", 
//         sensorDataCallback
//     );

//     std::cout << "[Subscriber] 节点启动，等待接收数据..." << std::endl;

//     // 4. 挂起主线程，启动事件轮询引擎 (极低 CPU 占用)
//     node.spin();

//     // 5. 清理系统资源
//     SystemManager::destroy();
//     return 0;
// }

// #include "com_ipc/api/subscriber.h"
// #include <iostream>
// #include <string>
// #include <thread>

// int main() {
//     std::cout << "=== Chassis Node (Subscriber) Started ===\n";
    
//     // 1. 使用我们刚才加的 QoS 预设：500ms 内必须收到新指令，否则认为上游死机
//     com_ipc::QoSProfile qos = com_ipc::QoSProfile::ControlCommand(500);
//     com_ipc::Subscriber<std::string> sub("cmd_vel", qos);

//     // 2. 【核心测试点】：注册看门狗急停回调
//     sub.registerDeadlineCallback([]() {
//         std::cerr << "\n🚨🚨🚨 [EMERGENCY STOP] 超过 500ms 未收到导航数据！\n"
//                   << "🚨🚨🚨 [执行动作]：底层电机已切断动力，底盘强制刹车！\n\n";
//     });

//     // 3. 正常的业务回调：打印收到的速度指令
//     sub.registerCallback([](const std::string* msg) {
//         std::cout << "[正常行驶] 收到导航控制帧: " << *msg << std::endl;
//     });

//     // 保持主线程存活
//     while (true) {
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//     }

//     return 0;
// }

#include "com_ipc/api/subscriber.h"
#include <iostream>
#include <thread>

// 1. 定义工业标准的底盘控制结构体（内存连续，完美支持零拷贝）
struct VelocityCmd {
    float v_x;
    float v_y;
    uint32_t seq;
};

int main() {
    std::cout << "=== Chassis Node (Subscriber) Started ===\n";
    
    com_ipc::QoSProfile qos = com_ipc::QoSProfile::ControlCommand(500);
    // 2. 订阅类型改为我们定义的结构体
    com_ipc::Subscriber<VelocityCmd> sub("cmd_vel", qos);

    // 3. 注册看门狗急停回调
    sub.registerDeadlineCallback([]() {
        std::cerr << "\n🚨🚨🚨 [EMERGENCY STOP] 超过 500ms 未收到导航数据！\n"
                  << "🚨🚨🚨 [执行动作]：底层电机已切断动力，底盘强制刹车！\n\n";
    });

    // 4. 正常的业务回调：通过指针直接读取共享内存，开销为 0
    sub.registerCallback([](const VelocityCmd* msg) {
        std::cout << "[正常行驶] 收到导航控制帧: v_x=" << msg->v_x 
                  << " m/s, seq=" << msg->seq << std::endl;
    });

    // 保持主线程存活
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}