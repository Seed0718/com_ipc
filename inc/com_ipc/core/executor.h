#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace com_ipc {

class MultiThreadedExecutor {
public:
    // 默认按照系统 CPU 核心数启动工作线程，防止物理线程过载
    MultiThreadedExecutor(size_t num_threads = std::thread::hardware_concurrency()) 
        : stop_(false) 
    {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex_);
                        this->condition_.wait(lock, [this] { 
                            return this->stop_ || !this->tasks_.empty(); 
                        });
                        
                        if (this->stop_ && this->tasks_.empty()) return;
                        
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    // 执行真实的算法回调（如 PGV 导航、避障算法）
                    task();
                }
            });
        }
    }

    ~MultiThreadedExecutor() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread &worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    // 将任何回调任务投递到线程池
    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace(std::forward<F>(f));
        }
        condition_.notify_one();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
};

} // namespace com_ipc