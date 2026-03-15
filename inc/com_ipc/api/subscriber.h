#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <cstring>
#include <iostream>
#include <sys/mman.h>

// 引入核心组件和 QoS 策略
#include "com_ipc/qos/qos_profile.h"
#include "com_ipc/core/com_ipc_types.h"
#include "com_ipc/core/system_manager.h"
#include "com_ipc/core/memory_pool.h"
#include "com_ipc/core/executor.h"

extern volatile sig_atomic_t g_shutdown_requested;

namespace com_ipc {

// ==========================================
// 1. 零拷贝消息载体 (跨类型通用元数据)
// ==========================================
struct LoanedMessage {
    void* data;
    size_t size;
    uint32_t seq;
};

// // 【新增】：任务投递器类型，彻底解耦执行器
// using TaskSubmitter = std::function<void(std::function<void()>)>;

// ==========================================
// 2. 非模板基类：处理 OS 级别的条件变量同步与共享内存读写
// ==========================================
class SubscriberBase {
public:
    SubscriberBase(const std::string& topic_name, const QoSProfile& qos = QoSProfile::Default());
    virtual ~SubscriberBase();

    uint32_t getLastSeq() const { return last_seq_; }

    // 面向工具链和底层框架的 Raw 接口
    int receiveRaw(void* buffer, size_t buffer_size, int timeout_ms = -1);
    bool receiveLoaned(LoanedMessage& msg, int timeout_ms = 1000);

    using RawMessageCallback = std::function<void(const LoanedMessage&)>;
    // void registerRawCallback(RawMessageCallback cb);
    // 【修改】：注册回调时，接收 Executor 指针
    void registerRawCallback(RawMessageCallback cb, MultiThreadedExecutor* executor = nullptr);

    // 【新增】：看门狗回调类型与注册接口
    using DeadlineCallback = std::function<void()>;
    void registerDeadlineCallback(DeadlineCallback cb);

protected:
    std::string topic_name_;
    QoSProfile qos_;
    TopicShm* shm_ptr_;
    uint32_t last_seq_; // 记录该订阅者读到的最新序号

    // 异步循环相关的底层资源
    
    RawMessageCallback raw_callback_;
    std::thread async_thread_;
    std::atomic<bool> async_running_{false};

    
    void asyncLoop(); // 后台死循环收件人

    // 【新增】：看门狗相关底层资源
    DeadlineCallback deadline_callback_;
    std::thread watchdog_thread_;
    std::atomic<bool> watchdog_running_{false};
    std::atomic<uint64_t> last_recv_time_ms_{0}; // 记录最后一次收到消息的绝对时间
    std::atomic<bool> is_alive_{false};          // 边缘触发状态机标记

    void watchdogLoop();           // 看门狗死循环
    uint64_t getCurrentTimeMs() const; // 获取系统时间的辅助函数

    MultiThreadedExecutor* executor_;

};

// ==========================================
// 3. 模板派生类：提供强类型的接收与回调接口
// ==========================================
template <typename T>
class Subscriber : public SubscriberBase {
public:
    // 强类型回调函数：用户直接拿到 T* 指针，彻底告别 void*
    using TypedMessageCallback = std::function<void(const T*)>;

    Subscriber(const std::string& topic_name, 
               const QoSProfile& qos = QoSProfile::Default())
        : SubscriberBase(topic_name, qos) {}

    // 阻塞式拷贝接收
    bool receive(T& msg, int timeout_ms = 1000) {
        char buffer[sizeof(MessageHeader) + MAX_MSG_SIZE];
        int ret = this->receiveRaw(buffer, sizeof(buffer), timeout_ms);
        if (ret <= 0) return false;
        
        MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buffer);
        if (hdr->data_size != sizeof(T)) return false; // 校验大小
        
        std::memcpy(&msg, buffer + sizeof(MessageHeader), sizeof(T));
        return true;
    }

    // 阻塞式零拷贝接收 (直接借出指针)
    T* loan(int timeout_ms = 1000) {
        LoanedMessage loaned_msg;
        if (this->receiveLoaned(loaned_msg, timeout_ms)) {
            return static_cast<T*>(loaned_msg.data);
        }
        return nullptr;
    }

    // // 注册强类型异步回调 (核心体验升级)
    // void registerCallback(TypedMessageCallback cb) {
    //     auto raw_cb = [cb](const LoanedMessage& msg) {
    //         // 收到底层 void* 数据后，强转为 T* 并直接丢给用户
    //         cb(static_cast<const T*>(msg.data));
    //     };
    //     this->registerRawCallback(raw_cb);
    // }

    // 【修改】：向下传递 Executor 指针
    void registerCallback(TypedMessageCallback cb, MultiThreadedExecutor* executor = nullptr) {
        auto raw_cb = [cb](const LoanedMessage& msg) {
            cb(static_cast<const T*>(msg.data));
        };
        this->registerRawCallback(raw_cb, executor);
    }
};

// ==========================================
// 4. 针对 std::string 的模板特化 (完全兼容你原来的逻辑)
// ==========================================
template <>
inline bool Subscriber<std::string>::receive(std::string& msg, int timeout_ms) {
    char buffer[sizeof(MessageHeader) + MAX_MSG_SIZE];
    int ret = this->receiveRaw(buffer, sizeof(buffer), timeout_ms);
    if (ret <= 0) return false;
    
    MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buffer);
    // 假设你有 MSG_STRING = 1 这个枚举，如果没有可以注释掉校验
    // if (hdr->type != 1) return false; 
    
    msg.assign(buffer + sizeof(MessageHeader), hdr->data_size);
    return true;
}

} // namespace com_ipc