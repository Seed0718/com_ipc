#ifndef COM_IPC_UDP_NODE_H
#define COM_IPC_UDP_NODE_H

#include <string>
#include <netinet/in.h>
#include "protocol.h" // 🌟 引入 QoS 协议规范

namespace com_ipc {

// ==========================================
// 🚀 UDP 发送端 (单播/多播皆可)
// ==========================================
class UDPPublisher {
public:
    UDPPublisher(const std::string& dest_ip, int port);
    ~UDPPublisher();
    bool publish(const void* data, size_t size);

private:
    int sock_;
    struct sockaddr_in dest_addr_;
    uint32_t current_seq_ = 0; // QoS状态
};

// ==========================================
// 📡 UDP 接收端 (阻塞接收跨网数据)
// ==========================================
class UDPSubscriber {
public:
    UDPSubscriber(const std::string& bind_ip, int port);
    ~UDPSubscriber();
    std::string receive();

private:
    int sock_;
    struct sockaddr_in local_addr_;
    uint32_t last_valid_seq_ = 0; // QoS状态
};

// ==========================================
// 🚀 跨网 RPC 专属：客户端身份记事本
// ==========================================
struct UDPClientAddress {
    struct sockaddr_in addr;
    socklen_t len;
    uint32_t request_id; // 🌟 QoS 会话锚点：记录这次请求的唯一 ID
};

// ==========================================
// 🚀 跨网 Service Client (带超时与 QoS)
// ==========================================
class UDPServiceClient {
public:
    UDPServiceClient(const std::string& server_ip, int port);
    ~UDPServiceClient();
    
    // 发起 RPC 调用。默认超时时间 500 毫秒 (Deadline QoS)
    std::string call(const void* request_data, size_t size, int timeout_ms = 500);

private:
    int sock_;
    struct sockaddr_in server_addr_;
    uint32_t current_request_id_ = 0;
};

// ==========================================
// 🛡️ 跨网 Service Server (精准回传与 QoS)
// ==========================================
class UDPServiceServer {
public:
    UDPServiceServer(const std::string& bind_ip, int port);
    ~UDPServiceServer();

    // 阻塞接收请求
    bool receive_request(std::string& out_request, UDPClientAddress& out_client_info);
    
    // 发送响应
    bool send_response(const void* response_data, size_t size, const UDPClientAddress& client_info);

private:
    int sock_;
    struct sockaddr_in local_addr_;
};

} // namespace com_ipc

#endif // COM_IPC_UDP_NODE_H