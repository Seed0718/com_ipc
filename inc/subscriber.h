#ifndef SUBSCRIBER_H
#define SUBSCRIBER_H

#include "com_ipc_types.h"
#include "system_manager.h"
#include "memory_pool.h"

// --- 订阅者 (Subscriber) ---
class Subscriber {
public:
    explicit Subscriber(const std::string& topic_name);
    ~Subscriber();

    // 核心接收函数，支持阻塞(-1)、非阻塞(0)及超时(>0)
    int receiveRaw(void* buffer, size_t buffer_size, int timeout_ms = -1);
    uint32_t getLastSeq() const { return last_seq_; }

    template<typename T>
    bool receive(T& msg, int timeout_ms = 1000) {
        char buffer[sizeof(MessageHeader) + MAX_MSG_SIZE];
        int ret = receiveRaw(buffer, sizeof(buffer), timeout_ms);
        if (ret <= 0) return false;
        
        MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buffer);
        if (hdr->data_size != sizeof(T)) return false; // 校验大小
        
        std::memcpy(&msg, buffer + sizeof(MessageHeader), sizeof(T));
        return true;
    }
    
    bool receive(std::string& msg, int timeout_ms = 1000) {
        char buffer[sizeof(MessageHeader) + MAX_MSG_SIZE];
        int ret = receiveRaw(buffer, sizeof(buffer), timeout_ms);
        if (ret <= 0) return false;
        
        MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buffer);
        if (hdr->type != MSG_STRING) return false; // 校验类型
        
        msg.assign(buffer + sizeof(MessageHeader), hdr->data_size);
        return true;
    }

    friend class ActionClient;
    friend class ActionServer;

private:
    std::string topic_name_;
    TopicShm* shm_ptr_;
    uint32_t last_seq_; // 记录该订阅者读到的最新序号，用于判断是否有新消息
};
#endif//SUBSCRIBER_H