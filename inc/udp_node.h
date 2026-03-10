// 文件：inc/udp_node.h
#ifndef COM_IPC_UDP_NODE_H
#define COM_IPC_UDP_NODE_H

#include <string>
#include <netinet/in.h>

namespace com_ipc {

// 🚀 UDP 发送端 (单播/多播皆可)
class UDPPublisher {
public:
    UDPPublisher(const std::string& dest_ip, int port);
    ~UDPPublisher();
    // 核心发送函数：接收任何类型的结构体指针和它的大小
    bool publish(const void* data, size_t size);

private:
    int sock_;
    struct sockaddr_in dest_addr_;
};

// 📡 UDP 接收端 (阻塞接收跨网数据)
class UDPSubscriber {
public:
    UDPSubscriber(const std::string& bind_ip, int port);
    ~UDPSubscriber();
    // 阻塞接收数据，直接返回二进制 string
    std::string receive();

private:
    int sock_;
    struct sockaddr_in local_addr_;
};

} // namespace com_ipc

#endif // COM_IPC_UDP_NODE_H