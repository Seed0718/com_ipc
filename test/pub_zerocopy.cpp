#include "com_ipc.h"
#include <iostream>
#include <thread>
#include <csignal>
#include <chrono>

using namespace com_ipc;

static volatile bool running = true;

void signal_handler(int) {
    running = false;
    g_shutdown_requested = 1;
}

// 定义一个极度夸张的大结构体（5MB），这在以前的 256 字节限制下是不可想象的
struct BigImage {
    int id;
    int width;
    int height;
    uint8_t pixels[5 * 1024 * 1024]; // 5MB 的像素数据
};

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    try {
        SystemManager::instance();

        Publisher<BigImage> pub("zero_copy_image");
        int seq = 0;

        std::cout << "Zero-Copy Publisher started. Sending 5MB images..." << std::endl;

        while (running) {
            // 【核心 1】：不创建局部变量，直接向内存池“借”一块 5MB 的空间
            BigImage* img_ptr = pub.loan();
            if (!img_ptr) {
                std::cerr << "Failed to loan memory!" << std::endl;
                break;
            }

            // 【核心 2】：就地写入数据！零拷贝！
            img_ptr->id = seq++;
            img_ptr->width = 1920;
            img_ptr->height = 1080;
            // 随便改写几个像素模拟图像变化
            img_ptr->pixels[0] = static_cast<char>(seq % 255); 
            img_ptr->pixels[5000000] = 'Z';

            // 【核心 3】：将填好数据的指针“还”给系统并发布出去
            if (pub.publishLoaned(img_ptr, sizeof(BigImage))) {
                std::cout << "[PUB] Zero-Copy Published Image #" << img_ptr->id 
                          << " (Size: 5MB)" << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 一秒发两张
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    SystemManager::destroy();
    return 0;
}