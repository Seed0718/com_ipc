#pragma once

#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

#include "com_ipc/api/publisher.h"
#include "com_ipc/api/subscriber.h"
#include "com_ipc/api/service_client.h"
#include "com_ipc/api/service_server.h"

namespace com_ipc {

class Node {
public:
    Node(const std::string& node_name) : node_name_(node_name), stop_(false) {
        std::cout << "[Node] " << node_name_ << " 初始化完毕. 等待 spin() 接管线程管理权." << std::endl;
    }

    ~Node() {
        stopSpin();
        for (auto p : pubs_) delete p;
        for (auto s : subs_) delete s;
    }

    std::string getName() const { return node_name_; }

    template<typename T>
    Publisher<T>* createPublisher(const std::string& topic_name) {
        auto pub = new Publisher<T>(topic_name);
        pubs_.push_back(pub);
        return pub;
    }

    // 【重构核心】：向 Subscriber 注入 Node 专属的并发投递队列
    template<typename T>
    Subscriber<T>* createSubscriber(const std::string& topic_name, std::function<void(const T*)> callback) {
        auto sub = new Subscriber<T>(topic_name);
        subs_.push_back(sub);
        
        // Lambda 捕获 this，使得 Subscriber 不需要知道 Node 的头文件
        sub->registerCallback(callback, [this](std::function<void()> task) {
            this->enqueueTask(std::move(task));
        });
        return sub;
    }

    // 【核心控制权】：用户调用 spin 时，才真正建立执行引擎
    void spin(size_t num_threads = std::thread::hardware_concurrency()) {
        if (num_threads == 0) num_threads = 1;
        
        std::cout << "[Node Executor] " << node_name_ << " 正式接管控制权，启动 " << num_threads << " 个并发执行流..." << std::endl;
        stop_ = false;

        // 启动 N-1 个后台辅助线程
        std::vector<std::thread> workers;
        for (size_t i = 0; i < num_threads - 1; ++i) {
            workers.emplace_back(&Node::workerLoop, this);
        }

        // 【精华】：让调用 spin() 的主线程也亲自下场干活，不再睡大觉！
        workerLoop();

        // 收到进程级强杀信号后，等待其余打工线程下班
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
        std::cout << "[Node Executor] " << node_name_ << " 退出 Spin." << std::endl;
    }

    // 提供给外部或内部的强制停止接口
    void stopSpin() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
    }

private:
    // 供内部或 Subscriber 调用的任务入队接口
    void enqueueTask(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            tasks_.push(std::move(task));
        }
        condition_.notify_one(); // 唤醒任何一个闲置的 worker
    }

    // 统一的工作状态机（主线程和辅助线程共用）
    void workerLoop() {
        while (!stop_ && !g_shutdown_requested) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                // 彻底休眠，直到有任务或系统退出，绝对的 0% CPU 占用
                condition_.wait(lock, [this] { 
                    return stop_ || g_shutdown_requested || !tasks_.empty(); 
                });
                
                if ((stop_ || g_shutdown_requested) && tasks_.empty()) return;
                
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            
            if (task) {
                task(); // 真正执行用户编写的高负载 AI、避障等算法回调
            }
        }
    }

    std::string node_name_;
    std::vector<PublisherBase*> pubs_;     
    std::vector<SubscriberBase*> subs_;    
    
    // Node 直接持有的线程调度同步原语
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
};

} // namespace com_ipc