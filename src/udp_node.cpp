// 文件：src/udp_node.cpp
#include "udp_node.h"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

namespace com_ipc {

// ================= UDPPublisher 实现 =================
UDPPublisher::UDPPublisher(const std::string& dest_ip, int port) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&dest_addr_, 0, sizeof(dest_addr_));
    dest_addr_.sin_family = AF_INET;
    dest_addr_.sin_addr.s_addr = inet_addr(dest_ip.c_str());
    dest_addr_.sin_port = htons(port);
}

UDPPublisher::~UDPPublisher() {
    if (sock_ >= 0) close(sock_);
}

bool UDPPublisher::publish(const void* data, size_t size) {
    if (sock_ < 0 || data == nullptr || size == 0) return false;
    ssize_t sent = sendto(sock_, data, size, 0, (struct sockaddr*)&dest_addr_, sizeof(dest_addr_));
    return sent == static_cast<ssize_t>(size);
}

// ================= UDPSubscriber 实现 =================
UDPSubscriber::UDPSubscriber(const std::string& bind_ip, int port) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    
    // 1. 开启端口复用：允许多个终端（多个进程）同时监听同一个端口
    int reuse = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(sock_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    // 2. 🎯 终极防弹绑定：必须绑定到 INADDR_ANY (0.0.0.0)，监听所有物理和虚拟网卡！
    memset(&local_addr_, 0, sizeof(local_addr_));
    local_addr_.sin_family = AF_INET;
    local_addr_.sin_port = htons(port);
    local_addr_.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(sock_, (struct sockaddr*)&local_addr_, sizeof(local_addr_)) < 0) {
        std::cerr << "[UDPSubscriber] 绑定端口失败!" << std::endl;
    }

    // 3. 🎯 核心魔法：向底层网卡发送 IGMP 指令，强行加入多播群落！
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(bind_ip.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    // 尝试加入多播组（如果传入的是 127.0.0.1 单播，这步会安静地失败，完全不影响接收）
    if (setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) >= 0) {
        std::cout << "[底层网络] 已向内核注册 IGMP 多播群落: " << bind_ip << std::endl;
    }
}

UDPSubscriber::~UDPSubscriber() {
    if (sock_ >= 0) close(sock_);
}

std::string UDPSubscriber::receive() {
    if (sock_ < 0) return "";
    char buffer[2048];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    
    ssize_t len = recvfrom(sock_, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &sender_len);
    if (len > 0) return std::string(buffer, len);
    return "";
}

} // namespace com_ipc