#include "com_ipc.h"
#include <iostream>
#include <csignal>

using namespace com_ipc;

static volatile bool running = true;

void signal_handler(int) {
    running = false;
    g_shutdown_requested = 1;
}

// 必须和发送端定义一模一样
struct BigImage {
    int id;
    int width;
    int height;
    char pixels[5 * 1024 * 1024]; 
};

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    try {
        SystemManager::instance();

        Subscriber<BigImage> sub("zero_copy_image");
        std::cout << "Zero-Copy Subscriber started. Waiting for 5MB images..." << std::endl;

        while (running) {
            LoanedMessage msg;
            
            // 【核心 1】：直接接收底层的原始指针，没有 memcpy！
            if (sub.receiveLoaned(msg, 1000)) {  
                // 【核心 2】：将空类型的指针强转回我们的图像结构体
                BigImage* img_ptr = static_cast<BigImage*>(msg.data);
                
                std::cout << "[SUB] Zero-Copy Received Image #" << img_ptr->id 
                          << " | Resolution: " << img_ptr->width << "x" << img_ptr->height
                          << " | Pixel[0]: " << static_cast<int>(img_ptr->pixels[0]) 
                          << " | Pixel[End]: " << img_ptr->pixels[5000000]
                          << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    SystemManager::destroy();
    return 0;
}