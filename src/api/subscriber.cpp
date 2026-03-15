#include "com_ipc/api/subscriber.h"
#include <iostream>

namespace com_ipc {

// ==================== SubscriberBase ====================

// 辅助函数：获取当前系统的毫秒时间戳
uint64_t SubscriberBase::getCurrentTimeMs() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

SubscriberBase::SubscriberBase(const std::string& topic_name, const QoSProfile& qos) 
    : topic_name_(topic_name), qos_(qos), shm_ptr_(nullptr), last_seq_(0) 
{
    // 轮询等待 Publisher 建立话题共享内存
    while (!g_shutdown_requested) {
        shm_ptr_ = SystemManager::instance()->createOrGetTopic(topic_name, false);
        if (shm_ptr_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (shm_ptr_) {
        SystemManager::instance()->lockRobust(&shm_ptr_->mutex);
        // 初始化时，把游标对齐到当前最新进度，避免接收陈旧数据
        last_seq_ = shm_ptr_->seq; 
        
        // 增加活跃订阅者计数
        if (shm_ptr_->active_subscribers >= 0) {
            shm_ptr_->active_subscribers++;
        }
        pthread_mutex_unlock(&shm_ptr_->mutex);

        std::cout << "[Subscriber] Connected to topic: " << topic_name_ 
                  << " | QoS History: " << (qos_.history == HistoryPolicy::KEEP_LAST ? "KEEP_LAST" : "KEEP_ALL") 
                  << std::endl;
    }
    last_recv_time_ms_ = getCurrentTimeMs(); // 构造时打下时间基准
}

SubscriberBase::~SubscriberBase() {
    if (async_running_) {
        async_running_ = false;
        if (async_thread_.joinable()) async_thread_.join();
    }
    // 【新增】：清理看门狗线程
    if (watchdog_running_) {
        watchdog_running_ = false;
        if (watchdog_thread_.joinable()) watchdog_thread_.join();
    }
    if (shm_ptr_) {
        SystemManager::instance()->lockRobust(&shm_ptr_->mutex);
        if (shm_ptr_->active_subscribers > 0) shm_ptr_->active_subscribers--;
        pthread_mutex_unlock(&shm_ptr_->mutex);
        munmap(shm_ptr_, sizeof(TopicShm));
    }
}

// void SubscriberBase::registerRawCallback(RawMessageCallback cb) {
//     raw_callback_ = cb;
//     if (!async_running_) {
//         async_running_ = true;
//         // 启动专属的后台收件小哥
//         async_thread_ = std::thread(&SubscriberBase::asyncLoop, this);
//     }
// }
void SubscriberBase::registerRawCallback(RawMessageCallback cb, MultiThreadedExecutor* executor) {
    raw_callback_ = cb;
    executor_ = executor; // 保存线程池引用
    if (!async_running_) {
        async_running_ = true;
        // 启动专门负责等待条件变量的 I/O 线程
        async_thread_ = std::thread(&SubscriberBase::asyncLoop, this);
    }
}

// void SubscriberBase::asyncLoop() {
//     while (async_running_ && !g_shutdown_requested) {
//         LoanedMessage msg;
//         // 500ms 醒来一次，检查有没有按 Ctrl+C，防死锁
//         if (receiveLoaned(msg, 500)) { 
//             if (raw_callback_) {
//                 raw_callback_(msg); 
//             }
//         }
//     }
// }
// void SubscriberBase::asyncLoop() {
//     while (async_running_ && !g_shutdown_requested) {
//         LoanedMessage msg;
//         // 500ms 醒来一次检查退出信号。这里只负责 Wait，无任何计算！
//         if (receiveLoaned(msg, 500)) { 
//             if (raw_callback_) {
//                 if (executor_) {
//                     // 【核心重构】：解耦！将耗时的算法回调抛入有限的线程池中执行
//                     // 彻底解决多个 Subscriber 同时收到数据时的线程爆炸问题
//                     executor_->enqueue([cb = raw_callback_, msg]() {
//                         cb(msg);
//                     });
//                 } else {
//                     // 降级兼容：如果没有传入 Executor，则在 I/O 线程原地执行
//                     raw_callback_(msg);
//                 }
//             }
//         }
//     }
// }
void SubscriberBase::asyncLoop() {
    while (async_running_ && !g_shutdown_requested) {
        LoanedMessage msg;
        if (receiveLoaned(msg, 500)) { 
            if (raw_callback_) {
                if (executor_) {
                    // 【修复点】：兼容 C++11 的写法
                    // 先把回调拷贝出来，然后再通过值传递给 lambda
                    auto cb = raw_callback_;
                    executor_->enqueue([cb, msg]() {
                        cb(msg);
                    });
                } else {
                    raw_callback_(msg);
                }
            }
        }
    }
}

int SubscriberBase::receiveRaw(void* buffer, size_t buffer_size, int timeout_ms) {
    if (!shm_ptr_ || g_shutdown_requested) return -1;

    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

    // 【重要】：使用 while 防止条件变量“虚假唤醒(Spurious Wakeup)”
    while (last_seq_ >= shm_ptr_->seq && !g_shutdown_requested) {
        if (timeout_ms == 0) { // 非阻塞
            pthread_mutex_unlock(&shm_ptr_->mutex);
            return -1;
        } else if (timeout_ms > 0) { // 超时等待
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000;
            }
            int ret = pthread_cond_timedwait(&shm_ptr_->cond_new_msg, &shm_ptr_->mutex, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&shm_ptr_->mutex);
                return -1;
            }
        } else { // 阻塞死等
            pthread_cond_wait(&shm_ptr_->cond_new_msg, &shm_ptr_->mutex);
        }
    }

    if (g_shutdown_requested) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return -1;
    }

    // 【修复潜在Bug】: 增加了 + BUFFER_SIZE，防止 write_index 为 0 时算出来是负数导致数组越界
    int slot = (shm_ptr_->write_index - 1 + BUFFER_SIZE) % BUFFER_SIZE;
    
    MessageHeader temp_header = shm_ptr_->buffer[slot].header;
    size_t data_size = temp_header.data_size;
    uint32_t offset = temp_header.pool_offset; // 获取取件码
    
    // 【核心新增】：在解锁前占住
    MemoryPool::instance()->addRef(offset);
    // 【核心性能优化】：拿到取件码就立刻解锁队列！不耽误其他进程发布数据
    pthread_mutex_unlock(&shm_ptr_->mutex);

    size_t total_size = sizeof(MessageHeader) + data_size;
    if (total_size <= buffer_size) {
        // 【去内存池提货】
        void* source_ptr = MemoryPool::instance()->getPointer(offset);
        std::memcpy(buffer, &temp_header, sizeof(MessageHeader));
        std::memcpy(static_cast<char*>(buffer) + sizeof(MessageHeader), source_ptr, data_size);
        last_seq_ = temp_header.seq;
        // 【新增】：刷新最后收件时间，并标记为存活！
        last_recv_time_ms_ = getCurrentTimeMs();
        is_alive_ = true;
        // 【核心新增】：本地拷贝已经结束，可以放手了！
        MemoryPool::instance()->releaseRef(offset);
        return static_cast<int>(total_size);
    }
    // 如果走到异常分支，也别忘了释放
    MemoryPool::instance()->releaseRef(offset);
    return -1;
}

bool SubscriberBase::receiveLoaned(LoanedMessage& msg, int timeout_ms) {
    if (!shm_ptr_ || g_shutdown_requested) return false;

    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

    while (last_seq_ >= shm_ptr_->seq && !g_shutdown_requested) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&shm_ptr_->mutex);
            return false;
        } else if (timeout_ms > 0) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000; }
            int ret = pthread_cond_timedwait(&shm_ptr_->cond_new_msg, &shm_ptr_->mutex, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&shm_ptr_->mutex);
                return false;
            }
        } else {
            pthread_cond_wait(&shm_ptr_->cond_new_msg, &shm_ptr_->mutex);
        }
    }

    if (g_shutdown_requested) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return false;
    }

    // 防溢出回环算法更新
    int slot = (shm_ptr_->write_index - 1 + shm_ptr_->capacity) % shm_ptr_->capacity;
    MessageHeader temp_header = shm_ptr_->buffer[slot].header;

    // 【核心新增】：在解锁之前，赶紧把这块内存的引用计数 +1，贴上护身符！
    MemoryPool::instance()->addRef(temp_header.pool_offset);
    
    // 拿到取件码立刻解锁，绝不阻塞发布者
    pthread_mutex_unlock(&shm_ptr_->mutex);

    // 【核心零拷贝】：不去 memcpy，直接把内存池的绝对指针甩给用户！
    msg.data = MemoryPool::instance()->getPointer(temp_header.pool_offset);
    msg.size = temp_header.data_size;
    msg.seq = temp_header.seq;
    
    last_seq_ = temp_header.seq;
    // 【新增】：刷新最后收件时间，并标记为存活！
    last_recv_time_ms_ = getCurrentTimeMs();
    is_alive_ = true;
    return true;
}

// 注册回调并拉起看门狗线程
void SubscriberBase::registerDeadlineCallback(DeadlineCallback cb) {
    deadline_callback_ = cb;
    // 只有在 QoS 中配置了 deadline > 0 才会真正拉起线程，零开销抽象！
    if (qos_.deadline_ms > 0 && !watchdog_running_) {
        watchdog_running_ = true;
        last_recv_time_ms_ = getCurrentTimeMs();
        is_alive_ = true;
        watchdog_thread_ = std::thread(&SubscriberBase::watchdogLoop, this);
    }
}

// 看门狗的后台幽灵线程
void SubscriberBase::watchdogLoop() {
    while (watchdog_running_ && !g_shutdown_requested) {
        // 睡眠 10ms 进行轮询，精度极高且完全不吃 CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
        
        auto now = getCurrentTimeMs();
        // 如果当前时间距离上次收件时间，超过了设定的 QoS 阈值
        if ((now - last_recv_time_ms_) > qos_.deadline_ms) {
            // 【核心防御】：is_alive_.exchange(false) 确保只有在断联的那个瞬间触发一次！
            // 防止一直调急停函数导致系统卡死
            if (is_alive_.exchange(false)) {
                if (deadline_callback_) {
                    std::cerr << "\n[QoS ALARM] Topic '" << topic_name_ 
                              << "' missed deadline of " << qos_.deadline_ms 
                              << "ms! Triggering Safety Callback!\n";
                    deadline_callback_();
                }
            }
        }
    }
}

} // namespace com_ipc