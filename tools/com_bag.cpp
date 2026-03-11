#include "com_ipc.h"
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <vector>
#include <cstring>

using namespace com_ipc;

static volatile bool running = true;

void signal_handler(int) {
    running = false;
    g_shutdown_requested = 1;
}

void print_usage() {
    std::cout << "==========================================\n"
              << "  com_bag - Data Record & Playback Tool\n"
              << "==========================================\n"
              << "Usage:\n"
              << "  com_bag record <topic_name> <file.bag>\n"
              << "  com_bag play <file.bag>\n"
              << "==========================================\n";
}

// 获取当前时间的微秒时间戳
int64_t get_current_time_us() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

// ==================== 录制模块 ====================
void do_record(const std::string& topic_name, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for writing.\n";
        return;
    }

    // 1. 写入文件头：魔法字符 "CBAG"
    const char magic[4] = {'C', 'B', 'A', 'G'};
    file.write(magic, 4);

    // 2. 写入话题名称长度和内容
    size_t topic_len = topic_name.length();
    file.write(reinterpret_cast<const char*>(&topic_len), sizeof(size_t));
    file.write(topic_name.c_str(), topic_len);

    SystemManager::instance();
    SubscriberBase sub(topic_name);
    std::cout << "Recording topic [" << topic_name << "] to [" << filename << "]...\n";
    std::cout << "Press Ctrl+C to stop.\n";

    int msg_count = 0;
    size_t total_bytes = 0;

    while (running) {
        LoanedMessage msg;
        // 阻塞等待数据
        if (sub.receiveLoaned(msg, 1000)) {
            int64_t timestamp = get_current_time_us();

            // 3. 写入数据帧头：时间戳 + 数据大小
            file.write(reinterpret_cast<const char*>(&timestamp), sizeof(int64_t));
            file.write(reinterpret_cast<const char*>(&msg.size), sizeof(size_t));

            // 4. 写入真正的二进制数据 (直接从零拷贝指针写入磁盘，速度极快)
            file.write(reinterpret_cast<const char*>(msg.data), msg.size);

            msg_count++;
            total_bytes += msg.size;
            
            // 每录制 10 条或者达到一定数据量打印一次进度
            if (msg_count % 10 == 0) {
                std::cout << "\rRecorded " << msg_count << " messages (" 
                          << total_bytes / 1024 << " KB)..." << std::flush;
            }
        }
    }
    
    std::cout << "\nRecording finished. Total messages: " << msg_count 
              << ", Total size: " << total_bytes / 1024 << " KB.\n";
    file.close();
}

// ==================== 回放模块 ====================
void do_play(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::in);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for reading.\n";
        return;
    }

    // 1. 校验魔法字符
    char magic[4];
    file.read(magic, 4);
    if (strncmp(magic, "CBAG", 4) != 0) {
        std::cerr << "Error: Invalid bag file format!\n";
        return;
    }

    // 2. 读出话题名称
    size_t topic_len;
    file.read(reinterpret_cast<char*>(&topic_len), sizeof(size_t));
    std::string topic_name(topic_len, '\0');
    file.read(&topic_name[0], topic_len);

    SystemManager::instance();
    PublisherBase pub(topic_name); // 根据读出的话题名自动创建发布者
    
    std::cout << "Playing back topic [" << topic_name << "] from [" << filename << "]...\n";

    int64_t prev_timestamp = 0;
    int msg_count = 0;

    while (running && file.peek() != EOF) {
        int64_t timestamp;
        size_t data_size;

        // 3. 读取数据帧头
        file.read(reinterpret_cast<char*>(&timestamp), sizeof(int64_t));
        file.read(reinterpret_cast<char*>(&data_size), sizeof(size_t));

        if (file.eof()) break;

        // 4. 精确的时间延时控制
        if (prev_timestamp != 0) {
            int64_t delay_us = timestamp - prev_timestamp;
            if (delay_us > 0) {
                // 模拟当年录制时的真实时间间隔
                std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
            }
        }
        prev_timestamp = timestamp;

        // 5. 零拷贝发布：直接向系统借内存，读进内存，然后发布！
        void* ptr = pub.loanRaw(data_size);
        if (ptr) {
            file.read(reinterpret_cast<char*>(ptr), data_size);
            pub.publishLoaned(ptr, data_size);
            msg_count++;
            
            if (msg_count % 10 == 0) {
                std::cout << "\rPlayed " << msg_count << " messages..." << std::flush;
            }
        } else {
            std::cerr << "\nError: Failed to loan memory for playback!\n";
            break;
        }
    }

    std::cout << "\nPlayback finished. Total messages played: " << msg_count << "\n";
    file.close();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];

    try {
        if (cmd == "record" && argc == 4) {
            do_record(argv[2], argv[3]);
        } else if (cmd == "play" && argc == 3) {
            do_play(argv[2]);
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