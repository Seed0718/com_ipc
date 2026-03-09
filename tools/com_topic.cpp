#include "com_ipc.h"
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <csignal>

static volatile bool running = true;

void signal_handler(int) {
    running = false;
    g_shutdown_requested = 1;
}

void print_usage() {
    std::cout << "==========================================\n"
              << "  com_topic - IPC Diagnostics Command Line\n"
              << "==========================================\n"
              << "Usage: com_topic <command> [topic_name]\n\n"
              << "Commands:\n"
              << "  list      List all active topics in the system\n"
              << "  hz        Display the publishing rate of a topic\n"
              << "  bw        Display the bandwidth used by a topic\n"
              << "  echo      Hex dump the raw memory of the topic\n"
              << "==========================================\n";
}

// 1. 列表功能：直接调用底层管理器的 list
void do_list() {
    std::cout << "Active Topics & Services:\n";
    std::cout << "-------------------------\n";
    SystemManager::instance()->listTopics();
    std::cout << "-------------------------\n";
}

// 2. 频率测算 (Hz)
void do_hz(const std::string& topic_name) {
    Subscriber sub(topic_name);
    std::cout << "Subscribed to [" << topic_name << "]. Calculating publishing rate...\n";
    
    auto last_calc_time = std::chrono::steady_clock::now();
    int msg_count = 0;
    bool is_first_msg = true; // 新增一个标志位

    while (running) {
        Subscriber::LoanedMessage msg;
        if (sub.receiveLoaned(msg, 1000)) {
            // 如果是第一帧，我们只按秒表，不计入统计帧数
            if (is_first_msg) {
                last_calc_time = std::chrono::steady_clock::now();
                is_first_msg = false;
                continue; 
            }
            msg_count++;
        }
        
        if (!is_first_msg) { // 只有收到过第一帧，才开始计算经过的时间
            auto now = std::chrono::steady_clock::now();
            double elapsed_sec = std::chrono::duration<double>(now - last_calc_time).count();
            
            if (elapsed_sec >= 1.0) {
                if (msg_count > 0) {
                    double hz = msg_count / elapsed_sec;
                    std::cout << "Average rate: " << std::fixed << std::setprecision(2) << hz << " Hz\n";
                }
                msg_count = 0;
                last_calc_time = now;
            }
        }
    }
}

// 3. 带宽测算 (Bandwidth)
void do_bw(const std::string& topic_name) {
    Subscriber sub(topic_name);
    std::cout << "Subscribed to [" << topic_name << "]. Calculating bandwidth...\n";
    
    auto last_calc_time = std::chrono::steady_clock::now();
    size_t total_bytes = 0;

    while (running) {
        Subscriber::LoanedMessage msg;
        if (sub.receiveLoaned(msg, 1000)) {
            total_bytes += msg.size;
        }
        
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - last_calc_time).count();
        
        if (elapsed_sec >= 1.0) {
            if (total_bytes > 0) {
                double kbps = (total_bytes / 1024.0) / elapsed_sec;
                double mbps = kbps / 1024.0;
                if (mbps > 1.0) {
                    std::cout << "Bandwidth: " << std::fixed << std::setprecision(2) << mbps << " MB/s\n";
                } else {
                    std::cout << "Bandwidth: " << std::fixed << std::setprecision(2) << kbps << " KB/s\n";
                }
            } else {
                std::cout << "No new messages...\n";
            }
            total_bytes = 0;
            last_calc_time = now;
        }
    }
}

// 4. 抓包探测 (Echo / Hex Dump)
void do_echo(const std::string& topic_name) {
    Subscriber sub(topic_name);
    std::cout << "Subscribed to [" << topic_name << "]. Sniffing raw memory...\n";

    while (running) {
        Subscriber::LoanedMessage msg;
        if (sub.receiveLoaned(msg, 1000)) {
            std::cout << "\n--- Message Seq: " << msg.seq << " | Size: " << msg.size << " bytes ---\n";
            
            // 安全打印前 32 个字节的十六进制和 ASCII 码
            const unsigned char* data = static_cast<const unsigned char*>(msg.data);
            size_t print_size = (msg.size < 32) ? msg.size : 32;
            
            for (size_t i = 0; i < print_size; ++i) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
            }
            if (msg.size > 32) std::cout << "... ";
            
            std::cout << " | ";
            
            for (size_t i = 0; i < print_size; ++i) {
                char c = data[i];
                if (c >= 32 && c <= 126) std::cout << c; // 可打印字符
                else std::cout << "."; // 不可打印字符用点代替
            }
            std::cout << std::dec << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];

    try {
        SystemManager::instance();

        if (cmd == "list") {
            do_list();
        } else if (cmd == "hz" && argc == 3) {
            do_hz(argv[2]);
        } else if (cmd == "bw" && argc == 3) {
            do_bw(argv[2]);
        } else if (cmd == "echo" && argc == 3) {
            do_echo(argv[2]);
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