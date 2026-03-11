#include "com_ipc.h"
#include "json.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <chrono>

using json = nlohmann::json;
using namespace com_ipc;

static volatile bool running = true;

void signal_handler(int) {
    running = false;
    g_shutdown_requested = 1;
}

void print_usage() {
    std::cout << "==========================================\n"
              << "  com_router - 究极 TCP 混合协议分布式网关\n"
              << "==========================================\n"
              << "用法:\n"
              << "  [发送端] ./com_router export <本地话题> <目标IP> <端口>\n"
              << "  [接收端] ./com_router import <监听端口>\n"
              << "==========================================\n";
}

int64_t get_current_time_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 保证数据完整收发的网络底层函数 (处理 TCP 粘包和分片)
bool send_all(int sock, const void* data, size_t size) {
    const char* buf = static_cast<const char*>(data);
    size_t total_sent = 0;
    while (total_sent < size && running) {
        ssize_t sent = send(sock, buf + total_sent, size - total_sent, 0);
        if (sent <= 0) return false;
        total_sent += sent;
    }
    return true;
}

bool recv_all(int sock, void* data, size_t size) {
    char* buf = static_cast<char*>(data);
    size_t total_recv = 0;
    while (total_recv < size && running) {
        ssize_t r = recv(sock, buf + total_recv, size - total_recv, 0);
        if (r > 0) {
            total_recv += r;
        } else if (r == 0) {
            return false; // 发送端断开了连接
        } else {
            // 核心魔法：如果是超时(EAGAIN)或被Ctrl+C中断(EINTR)，不要报错，继续循环检查 running！
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            return false; // 发生了真正的网络错误
        }
    }
    return total_recv == size;
}

// ==================== 发送端 (Export): 抓取本地 IPC，封包发送 ====================
void do_export(const std::string& topic_name, const std::string& ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("[Export] Socket 创建失败"); return; }

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);

    std::cout << "[Export] 正在连接目标节点 " << ip << ":" << port << "...\n";
    while (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0 && running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!running) return;

    std::cout << "[Export] 连接成功！启动 HYBR(混合) 协议导出...\n";

    // 动态挂载到底层共享内存
    SubscriberBase sub(topic_name, QoSProfile::Default());
    std::vector<char> local_buffer(sizeof(MessageHeader) + MAX_MSG_SIZE);
    int msg_count = 0;

    while (running) {
        // 阻塞接收本地共享内存数据
        int ret = sub.receiveRaw(local_buffer.data(), local_buffer.size(), 500);
        if (ret > 0) {
            MessageHeader* hdr = reinterpret_cast<MessageHeader*>(local_buffer.data());
            const void* payload = local_buffer.data() + sizeof(MessageHeader);
            uint32_t payload_len = hdr->data_size;

            // 1. 构造极其精简的 JSON 元数据，并瞬间转化为 MessagePack 二进制
            json meta = {
                {"topic", topic_name},
                {"type", hdr->type},
                {"seq", msg_count++},
                {"ts", get_current_time_us()}
            };
            std::vector<uint8_t> meta_msgpack = json::to_msgpack(meta);
            uint32_t meta_len = meta_msgpack.size();

            // 2. 发送 [魔法字] + [Meta长度] + [Payload长度] + [MsgPack数据] + [裸二进制负载]
            if (!send_all(sock, "HYBR", 4)) break;                   
            if (!send_all(sock, &meta_len, sizeof(uint32_t))) break; 
            if (!send_all(sock, &payload_len, sizeof(uint32_t))) break; 
            if (!send_all(sock, meta_msgpack.data(), meta_len)) break;  
            if (!send_all(sock, payload, payload_len)) break;        // 核心：百万字节直接送入 TCP 缓冲区！
            
            std::cout << "\r[Export] 已发送 " << msg_count << " 帧... (负载: " 
                      << payload_len / 1024 << " KB)" << std::flush;
        }
    }
    close(sock);
    std::cout << "\n[Export] 连接断开。\n";
}

// ==================== 接收端 (Import): 监听 TCP，解析并注入本地 IPC ====================
void do_import(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("[Import] 端口绑定失败"); close(server_fd); return;
    }
    if (listen(server_fd, 3) < 0) {
        perror("[Import] 监听失败"); close(server_fd); return;
    }

    std::cout << "[Import] 正在监听端口 " << port << " 等待接入...\n";

    struct sockaddr_in client_addr{};
    socklen_t addrlen = sizeof(client_addr);
    int client_sock = -1;

    // 核心改造：使用 select 实现带超时的非阻塞等待，完美响应 Ctrl+C！
    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000; // 500 毫秒醒来一次

        int ret = select(server_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ret > 0) {
            // 有人连接了！立刻 accept
            client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
            break; 
        } else if (ret < 0 && errno != EINTR) {
            // 发生真正的网络错误
            perror("[Import] Select 监听异常");
            break;
        }
        // 如果 ret == 0，说明超时了，循环回到起点，此时会去检查 running 是否还是 true
    }

    // 检查到底是连上了，还是被 Ctrl+C 强杀了
    if (!running || client_sock < 0) {
        std::cout << "\n[Import] 收到退出指令，优雅取消监听并释放端口...\n";
        close(server_fd);
        return;
    }
    
    std::cout << "[Import] 发送端已接入: " << inet_ntoa(client_addr.sin_addr) << "!\n";

    // 👇 加在这里：给接收端设置 500 毫秒的超时心跳
    struct timeval tv;
    tv.tv_sec = 0; tv.tv_usec = 500000;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    std::map<std::string, PublisherBase*> local_publishers;
    int msg_count = 0;

    while (running) {
        // 1. 接收握手协议
        char magic[4];
        if (!recv_all(client_sock, magic, 4)) break;
        if (strncmp(magic, "HYBR", 4) != 0) {
            std::cerr << "\n[Import] 协议魔法字校验失败！断开连接。\n"; 
            break;
        }

        // 2. 接收头部长度信息
        uint32_t meta_len, payload_len;
        if (!recv_all(client_sock, &meta_len, sizeof(uint32_t))) break;
        if (!recv_all(client_sock, &payload_len, sizeof(uint32_t))) break;

        // 3. 接收并解析 MessagePack 元数据
        std::vector<uint8_t> meta_buf(meta_len);
        if (!recv_all(client_sock, meta_buf.data(), meta_len)) break;
        
        json meta = json::from_msgpack(meta_buf);
        std::string topic = meta["topic"];
        int msg_type = meta.value("type", MSG_CUSTOM);

        // 4. 动态创建本地 Publisher (Type Erasure)
        if (local_publishers.find(topic) == local_publishers.end()) {
            std::cout << "\n[Import] 发现新话题 [" << topic << "]，已接入本地内存池。\n";
            local_publishers[topic] = new PublisherBase(topic, QoSProfile::Default());
        }

        // 5. 接收百万级纯二进制载荷，并直接拍进本地内存池！
        std::vector<char> payload_buf(payload_len);
        if (!recv_all(client_sock, payload_buf.data(), payload_len)) break;

        local_publishers[topic]->publishRaw(payload_buf.data(), payload_len, static_cast<MessageType>(msg_type));
        
        if (++msg_count % 10 == 0) {
            std::cout << "\r[Import] 已接收并转发 " << msg_count << " 帧... (负载: " 
                      << payload_len / 1024 << " KB)" << std::flush;
        }
    }

    std::cout << "\n[Import] 传输结束。\n";
    for (auto& pair : local_publishers) delete pair.second;
    close(client_sock);
    close(server_fd);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    if (argc < 2) { print_usage(); return 1; }
    std::string cmd = argv[1];

    try {
        if (cmd == "export" && argc == 5) {
            do_export(argv[2], argv[3], std::stoi(argv[4]));
        } else if (cmd == "import" && argc == 3) {
            do_import(std::stoi(argv[2]));
        } else {
            print_usage();
        }
    } catch (const std::exception& e) {
        std::cerr << "致命错误: " << e.what() << '\n';
        return 1;
    }
    return 0;
}