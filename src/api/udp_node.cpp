// 文件：udp_node.cpp
#include "com_ipc/api/udp_node.h"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <chrono> // 引入时间库用于纳秒/微秒级时间戳

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

    // 1. 💌 组装 QoS 信封
    QoSHeader header;
    header.seq_id = ++current_seq_; // 序列号自增
    
    // 获取当前微秒时间戳
    auto now = std::chrono::system_clock::now();
    header.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    
    header.msg_type = 0x00; // 默认普通 Topic
    header.qos_flag = 0x00; // 默认 Best Effort

    // 2. 📦 封装巨无霸数据包 (Header + Payload)
    std::vector<uint8_t> packet(sizeof(QoSHeader) + size);
    std::memcpy(packet.data(), &header, sizeof(QoSHeader));
    std::memcpy(packet.data() + sizeof(QoSHeader), data, size);

    // 3. 🚀 发射
    ssize_t sent = sendto(sock_, packet.data(), packet.size(), 0, (struct sockaddr*)&dest_addr_, sizeof(dest_addr_));
    return sent == static_cast<ssize_t>(packet.size());
}

// ================= UDPSubscriber 实现 =================
UDPSubscriber::UDPSubscriber(const std::string& bind_ip, int port) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    
    int reuse = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(sock_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    memset(&local_addr_, 0, sizeof(local_addr_));
    local_addr_.sin_family = AF_INET;
    local_addr_.sin_port = htons(port);
    local_addr_.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(sock_, (struct sockaddr*)&local_addr_, sizeof(local_addr_)) < 0) {
        std::cerr << "[UDPSubscriber] 绑定端口失败!" << std::endl;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(bind_ip.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) >= 0) {
        std::cout << "[底层网络] 已向内核注册 IGMP 多播群落: " << bind_ip << std::endl;
    }
}

UDPSubscriber::~UDPSubscriber() {
    if (sock_ >= 0) close(sock_);
}

std::string UDPSubscriber::receive() {
    if (sock_ < 0) return "";
    
    // 开辟足够大的缓冲区 (64KB，满足常规视觉外的控制指令绰绰有余)
    char buffer[65536]; 
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    
    ssize_t len = recvfrom(sock_, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &sender_len);
    
    // 🛡️ 拦截 1：如果连个信封的大小都不够，直接当作垃圾包丢弃
    if (len < static_cast<ssize_t>(sizeof(QoSHeader))) {
        return ""; 
    }

    // 💌 读取信封信息
    QoSHeader* header = reinterpret_cast<QoSHeader*>(buffer);

    // 🛡️ 拦截 2：C++ 底层防乱序引擎 (时光倒流检测)
    if (last_valid_seq_ != 0 && header->seq_id <= last_valid_seq_) {
        // 发现幽灵过期包！在底层默默斩杀，绝不向 Python 上层抛出
        return ""; 
    }

    // 更新安全时间线
    last_valid_seq_ = header->seq_id;

    // 🎁 拆包：剥离 14 字节的 QoS 信封，只提取纯净的业务数据抛给 Python
    size_t payload_size = len - sizeof(QoSHeader);
    if (payload_size > 0) {
        return std::string(buffer + sizeof(QoSHeader), payload_size);
    }
    
    return "";
}

// ================= UDPServiceClient 实现 =================
UDPServiceClient::UDPServiceClient(const std::string& server_ip, int port) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_addr.s_addr = inet_addr(server_ip.c_str());
    server_addr_.sin_port = htons(port);
}

UDPServiceClient::~UDPServiceClient() {
    if (sock_ >= 0) close(sock_);
}

std::string UDPServiceClient::call(const void* request_data, size_t size, int timeout_ms) {
    if (sock_ < 0 || request_data == nullptr || size == 0) return "";

    // 1. 💌 组装 Request 信封 (QoS)
    QoSHeader header;
    header.seq_id = ++current_request_id_; // 生成本次会话专属 ID
    auto now = std::chrono::system_clock::now();
    header.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    header.msg_type = 0x01; // 🌟 路由：0x01 代表 Request
    header.qos_flag = 0x01; // 要求可靠响应

    std::vector<uint8_t> packet(sizeof(QoSHeader) + size);
    std::memcpy(packet.data(), &header, sizeof(QoSHeader));
    std::memcpy(packet.data() + sizeof(QoSHeader), request_data, size);

    // 2. 🚀 发送请求
    sendto(sock_, packet.data(), packet.size(), 0, (struct sockaddr*)&server_addr_, sizeof(server_addr_));

    // 3. ⏳ 核心 QoS：设置 Socket 超时时间，绝生死锁防线！
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 4. 等待回音 (带超时打断)
    char buffer[65536];
    struct sockaddr_in reply_addr;
    socklen_t reply_len = sizeof(reply_addr);
    
    // 循环过滤网络里的杂音包
    while (true) {
        ssize_t len = recvfrom(sock_, buffer, sizeof(buffer), 0, (struct sockaddr*)&reply_addr, &reply_len);
        
        if (len < 0) {
            // 超时触发！C++ 底层立刻打破阻塞，保护上层 Python 不被死锁
            std::cerr << "⚠️ [UDPServiceClient] RPC 调用超时! (" << timeout_ms << "ms)" << std::endl;
            return ""; 
        }

        if (len >= static_cast<ssize_t>(sizeof(QoSHeader))) {
            QoSHeader* reply_header = reinterpret_cast<QoSHeader*>(buffer);
            
            // 🎯 会话双重校验：必须是响应包 (0x02)，且 Session ID 完美匹配
            if (reply_header->msg_type == 0x02 && reply_header->seq_id == current_request_id_) {
                size_t payload_size = len - sizeof(QoSHeader);
                return std::string(buffer + sizeof(QoSHeader), payload_size);
            }
        }
    }
}

// ================= UDPServiceServer 实现 =================
UDPServiceServer::UDPServiceServer(const std::string& bind_ip, int port) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    memset(&local_addr_, 0, sizeof(local_addr_));
    local_addr_.sin_family = AF_INET;
    local_addr_.sin_port = htons(port);
    // 绑定所有的网卡IP，监听指定端口
    local_addr_.sin_addr.s_addr = htonl(INADDR_ANY); 

    bind(sock_, (struct sockaddr*)&local_addr_, sizeof(local_addr_));
}

UDPServiceServer::~UDPServiceServer() {
    if (sock_ >= 0) close(sock_);
}

bool UDPServiceServer::receive_request(std::string& out_request, UDPClientAddress& out_client_info) {
    char buffer[65536];
    out_client_info.len = sizeof(out_client_info.addr);
    
    ssize_t len = recvfrom(sock_, buffer, sizeof(buffer), 0, (struct sockaddr*)&(out_client_info.addr), &(out_client_info.len));
    
    if (len >= static_cast<ssize_t>(sizeof(QoSHeader))) {
        QoSHeader* header = reinterpret_cast<QoSHeader*>(buffer);
        
        // 🎯 严格安检：只拦截并放行 Request (0x01) 包
        if (header->msg_type == 0x01) {
            out_client_info.request_id = header->seq_id; // 记下 Request ID
            size_t payload_size = len - sizeof(QoSHeader);
            out_request = std::string(buffer + sizeof(QoSHeader), payload_size);
            return true;
        }
    }
    return false;
}

bool UDPServiceServer::send_response(const void* response_data, size_t size, const UDPClientAddress& client_info) {
    if (sock_ < 0 || response_data == nullptr || size == 0) return false;

    // 1. 💌 组装 Response 信封
    QoSHeader header;
    header.seq_id = client_info.request_id; // 🌟 QoS：原封不动塞回客户端的 Request ID
    auto now = std::chrono::system_clock::now();
    header.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    header.msg_type = 0x02; // 🌟 路由：0x02 代表 Response
    header.qos_flag = 0x00;

    std::vector<uint8_t> packet(sizeof(QoSHeader) + size);
    std::memcpy(packet.data(), &header, sizeof(QoSHeader));
    std::memcpy(packet.data() + sizeof(QoSHeader), response_data, size);

    // 2. 🚀 精准制导，顺着网线打回给请求方
    ssize_t sent = sendto(sock_, packet.data(), packet.size(), 0, (struct sockaddr*)&(client_info.addr), client_info.len);
    return sent == static_cast<ssize_t>(packet.size());
}

} // namespace com_ipc