#include "com_ipc.h"
#include "json.hpp"
#include <iostream>
#include <string>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

using json = nlohmann::json;

static volatile bool running = true;

void signal_handler(int) {
    running = false;
    g_shutdown_requested = 1;
}

void print_usage() {
    std::cout << "==========================================\n"
              << "  com_router - Distributed Network Gateway\n"
              << "==========================================\n"
              << "Usage:\n"
              << "  com_router export <topic> <target_ip> <port>  (Send to network)\n"
              << "  com_router import <port>                      (Receive from network)\n"
              << "==========================================\n";
}

// 获取当前微秒时间戳的辅助函数
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
        if (r <= 0) return false;
        total_recv += r;
    }
    return true;
}

// ==================== 发送端：将本地话题导出到网络 ====================
// ==================== 发送端 (升级为混合协议) ====================
void do_export(const std::string& topic_name, const std::string& ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("Socket creation failed"); return; }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);

    std::cout << "Connecting to " << ip << ":" << port << "...\n";
    while (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0 && running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!running) return;

    std::cout << "Connected! Exporting Hybrid Protocol to network...\n";

    SystemManager::instance();
    Subscriber sub(topic_name);
    int msg_count = 0;

    while (running) {
        Subscriber::LoanedMessage msg;
        if (sub.receiveLoaned(msg, 1000)) {
            // 1. 极其优雅地构造 Meta 信息 (像写 JSON 一样)
            json meta;
            meta["topic"] = topic_name;
            meta["seq"] = msg_count++;
            meta["timestamp"] = get_current_time_us();
            meta["payload_size"] = msg.size;

            // 2. 瞬间将其转化为 MessagePack 二进制流！
            std::vector<uint8_t> meta_msgpack = json::to_msgpack(meta);

            uint32_t meta_len = meta_msgpack.size();
            uint32_t payload_len = msg.size;

            // 3. 发送混合网络包
            if (!send_all(sock, "HYBR", 4)) break;                   // 魔法字符
            if (!send_all(sock, &meta_len, sizeof(uint32_t))) break; // Meta 长度
            if (!send_all(sock, &payload_len, sizeof(uint32_t))) break; // Payload 长度
            if (!send_all(sock, meta_msgpack.data(), meta_len)) break;  // Meta 数据 (MsgPack)
            if (!send_all(sock, msg.data, payload_len)) break;          // Payload 裸数据 (0拷贝直发)

            if (msg_count % 10 == 0) std::cout << "\rExported " << msg_count << " hybrid frames..." << std::flush;
        }
    }
    close(sock);
}
// ==================== 接收端：从网络导入话题到本地 ====================
void do_import(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed"); close(server_fd); return;
    }
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed"); close(server_fd); return;
    }

    std::cout << "Listening on port " << port << " for incoming topics...\n";

    int client_sock;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    
    // 阻塞等待发送端连接
    if ((client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen)) < 0) {
        perror("Accept failed"); close(server_fd); return;
    }
    
    std::cout << "Client connected from " << inet_ntoa(client_addr.sin_addr) << "!\n";

    // 1. 接收握手协议
    char magic[4];
    if (!recv_all(client_sock, magic, 4) || strncmp(magic, "CIPC", 4) != 0) {
        std::cerr << "Invalid protocol magic!\n"; close(client_sock); close(server_fd); return;
    }

    size_t topic_len;
    recv_all(client_sock, &topic_len, sizeof(size_t));
    std::string topic_name(topic_len, '\0');
    recv_all(client_sock, &topic_name[0], topic_len);

    std::cout << "Importing topic [" << topic_name << "] to local SHM...\n";

    SystemManager::instance();
    Publisher pub(topic_name);
    int msg_count = 0;

    while (running) {
        size_t data_size;
        // 2. 接收帧头
        if (!recv_all(client_sock, &data_size, sizeof(size_t))) break;

        // 3. 终极性能奥义：直接向系统借用共享内存，让网卡直接把数据塞进去！
        void* ptr = pub.loanRaw(data_size);
        if (!ptr || !recv_all(client_sock, ptr, data_size)) break;

        pub.publishLoaned(ptr, data_size);
        if (++msg_count % 10 == 0) std::cout << "\rImported " << msg_count << " frames..." << std::flush;
    }

    std::cout << "\nImport terminated.\n";
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
        std::cerr << "Fatal Error: " << e.what() << '\n';
        return 1;
    }
    SystemManager::destroy();
    return 0;
}