#ifndef PUBLISHER_H
#define PUBLISHER_H


#include "com_ipc_types.h"
#include "system_manager.h"
#include "memory_pool.h"

// --- 发布者 (Publisher) ---
class Publisher {
public:
    explicit Publisher(const std::string& topic_name);
    ~Publisher();

    // 底层原始数据发送
    bool publishRaw(const void* data, size_t size, MessageType type = MSG_CUSTOM);

    // 模板方法，提供类型安全的结构体发送
    template<typename T>
    bool publish(const T& msg, MessageType type = MSG_CUSTOM) {
        static_assert(sizeof(T) <= MAX_MSG_SIZE, "Message size exceeds MAX_MSG_SIZE");
        return publishRaw(&msg, sizeof(T), type);
    }
    // 字符串重载
    bool publish(const std::string& msg) {
        return publishRaw(msg.c_str(), msg.size() + 1, MSG_STRING);
    }

    // ==================== 零拷贝专区 ====================
    // 1. 向共享内存“借”一块空间，返回强类型指针
    template<typename T>
    T* loan() {
        return static_cast<T*>(loanRaw(sizeof(T)));
    }
    void* loanRaw(size_t size);

    // 2. 将借出的空间直接发布出去 (无需拷贝)
    bool publishLoaned(void* data_ptr, size_t size, MessageType type = MSG_CUSTOM);

    std::string getTopic() const { return topic_name_; }

    friend class ActionServer;
    friend class ActionClient;

private:
    std::string topic_name_;
    TopicShm* shm_ptr_; // 直接持有共享内存指针
};

#endif//PUBLISHER_H