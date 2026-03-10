#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

// 【核心魔法】：强制编译器按 1 字节紧凑排列，绝不偷偷塞“空气”！
#pragma pack(push, 1)
struct ChassisState {
    uint64_t timestamp_us; // 8
    float speed_m_s;       // 4
    float steering_angle;  // 4
    int gear;              // 4
};
#pragma pack(pop)

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    dest_addr.sin_port = htons(9999);

    std::cout << "🚀 UDP 单播发射塔已启动 (目标: 127.0.0.1:9999)" << std::endl;
    // 打印一下结构体大小，你会看到它是完美的 20 字节
    std::cout << "📦 当前载荷结构体大小: " << sizeof(ChassisState) << " 字节" << std::endl;
    
    ChassisState state = {0, 0.0f, 0.5f, 1};

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        state.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        state.speed_m_s += 0.1f; 
        if (state.speed_m_s > 30.0f) state.speed_m_s = 0.0f; 

        sendto(sock, &state, sizeof(state), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        static int print_counter = 0;
        if (print_counter++ % 10 == 0) {
            std::cout << "[发送中] 当前车速: " << state.speed_m_s << " m/s" << std::endl;
        }
        usleep(10000); 
    }
    close(sock);
    return 0;
}