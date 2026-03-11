#include <iostream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "com_ipc/api/subscriber.h"
#include "json.hpp"

using namespace com_ipc;
using json = nlohmann::json;

// 🎯 核心修改：精准狙击你的香橙派 IP！
#define TARGET_IP "192.168.1.102"
#define TARGET_PORT 7400
#define MAX_UDP_PACKET_SIZE 65535

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <要转发的本地话题名称>" << std::endl;
        return -1;
    }
    std::string topic_name = argv[1];

    std::cout << "=== [Sender] 跨主机 UDP 单播发送网关启动 ===" << std::endl;
    std::cout << "正在桥接本地话题: [" << topic_name << "] -> 香橙派 " << TARGET_IP << ":" << TARGET_PORT << std::endl;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TARGET_PORT);
    dest_addr.sin_addr.s_addr = inet_addr(TARGET_IP);

    SubscriberBase sub(topic_name, QoSProfile::Default());
    std::vector<char> local_buffer(sizeof(MessageHeader) + MAX_MSG_SIZE);

    while (!g_shutdown_requested) {
        int ret = sub.receiveRaw(local_buffer.data(), local_buffer.size(), 100);
        if (ret > 0) {
            MessageHeader* hdr = reinterpret_cast<MessageHeader*>(local_buffer.data());
            const void* payload = local_buffer.data() + sizeof(MessageHeader);
            size_t payload_size = hdr->data_size;

            json json_header = {{"topic", topic_name}, {"size", payload_size}, {"type", hdr->type}};
            std::string header_str = json_header.dump() + "\r\n\r\n";

            if (header_str.size() + payload_size > MAX_UDP_PACKET_SIZE) continue;

            std::vector<char> udp_packet;
            udp_packet.reserve(header_str.size() + payload_size);
            udp_packet.insert(udp_packet.end(), header_str.begin(), header_str.end());
            const char* payload_chars = static_cast<const char*>(payload);
            udp_packet.insert(udp_packet.end(), payload_chars, payload_chars + payload_size);

            sendto(sock, udp_packet.data(), udp_packet.size(), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
            std::cout << "-> [Net] 成功单播转发话题 " << topic_name << " (" << payload_size << " bytes) 到香橙派" << std::endl;
        }
    }
    close(sock);
    return 0;
}