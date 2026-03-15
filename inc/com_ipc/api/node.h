#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <chrono>
#include <iostream>

#include "com_ipc/api/publisher.h"
#include "com_ipc/api/subscriber.h"
#include "com_ipc/api/service_client.h"
#include "com_ipc/api/service_server.h"
#include "com_ipc/core/executor.h"

namespace com_ipc {

class Node {
public:
    // 构造时可指定线程池大小，默认 4 线程
    Node(const std::string& node_name, size_t num_threads = 4) 
        : node_name_(node_name), executor_(num_threads) {
        std::cout << "[Node] " << node_name_ << " 已创建 (Executor Threads: " << num_threads << ")." << std::endl;
    }

    ~Node() {
        for (auto p : pubs_) delete p;
        for (auto s : subs_) delete s;
    }

    std::string getName() const { return node_name_; }

    // ==========================================
    // 1. 创建发布者 (直接透传)
    // ==========================================
    template<typename T>
    Publisher<T>* createPublisher(const std::string& topic_name) {
        auto pub = new Publisher<T>(topic_name);
        pubs_.push_back(pub);
        return pub;
    }

    // // ==========================================
    // // 2. 创建订阅者 (核心：挂载到事件循环，不启动野生线程！)
    // // ==========================================
    // template<typename T>
    // Subscriber<T>* createSubscriber(const std::string& topic_name, std::function<void(const T*)> callback) {
    //     auto sub = new Subscriber<T>(topic_name);
    //     subs_.push_back(sub);
        
    //     // 【关键设计】：我们将非阻塞的轮询逻辑打包成 lambda，塞进 Node 的大循环队列里
    //     spin_tasks_.push_back([sub, callback]() -> bool {
    //         // timeout_ms = 0，意味着去内存池看一眼，没有数据立刻返回 nullptr，绝不卡死！
    //         T* msg = sub->loan(0); 
    //         if (msg) {
    //             callback(msg);
    //             return true; // 告诉大循环：我刚才干活了！
    //         }
    //         return false;
    //     });
        
    //     return sub;
    // }

    // 【重构核心】：不再使用轮询，而是将 Executor 的指针传递给 Subscriber
    template<typename T>
    Subscriber<T>* createSubscriber(const std::string& topic_name, std::function<void(const T*)> callback) {
        auto sub = new Subscriber<T>(topic_name);
        subs_.push_back(sub);
        
        // 注册回调时，传入执行器指针
        sub->registerCallback(callback, &executor_);
        return sub;
    }

    // 阻塞主线程，维持节点生命周期，彻底告别 while(true) + sleep
    void spin() {
        std::cout << "[Node Executor] " << node_name_ << " 开始事件驱动 Spin..." << std::endl;
        // 阻塞主线程等待中断信号（工作全交给底层 Executor 和 I/O 监听线程）
        SystemManager::instance()->spin(); 
        std::cout << "[Node Executor] " << node_name_ << " 退出 Spin." << std::endl;
    }

    // // ==========================================
    // // 3. 执行器核心：统一的 Spin 调度引擎
    // // ==========================================
    // void spin() {
    //     std::cout << "[Node Executor] " << node_name_ << " 开始单线程并发 Spin 循环..." << std::endl;
    //     while (!g_shutdown_requested) {
    //         bool is_busy = spinOnce();
            
    //         // 如果所有的 Subscriber 都没有新数据，就让出 CPU 休眠 1 毫秒
    //         // 这既保证了微秒级的极速响应，又保证了 CPU 占用率接近 0%！
    //         if (!is_busy) {
    //             std::this_thread::sleep_for(std::chrono::milliseconds(1));
    //         }
    //     }
    //     std::cout << "[Node Executor] " << node_name_ << " 退出 Spin." << std::endl;
    // }

    // 执行一次非阻塞遍历
    bool spinOnce() {
        bool any_task_executed = false;
        for (auto& task : spin_tasks_) {
            if (task()) {
                any_task_executed = true; // 只要有一个任务处理了数据，就标记为繁忙
            }
        }
        return any_task_executed;
    }

private:
    std::string node_name_;
    std::vector<PublisherBase*> pubs_;     // 用于析构时释放内存
    std::vector<SubscriberBase*> subs_;    // 用于析构时释放内存
    
    // 存储所有非阻塞轮询任务的“事件循环队列”
    std::vector<std::function<bool()>> spin_tasks_; 
    MultiThreadedExecutor executor_;
};

} // namespace com_ipc