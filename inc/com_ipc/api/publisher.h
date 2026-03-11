#pragma once

#include <string>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>

#include "com_ipc/qos/qos_profile.h"
#include "com_ipc/core/memory_pool.h"
#include "com_ipc/core/system_manager.h"
#include "com_ipc/core/com_ipc_types.h"

extern volatile sig_atomic_t g_shutdown_requested;

namespace com_ipc {

class PublisherBase {
public:
    // 1. 确保构造函数有默认 QoS 参数
    PublisherBase(const std::string& topic_name, const QoSProfile& qos = QoSProfile::Default());
    virtual ~PublisherBase();

    // 2. 【关键】将这些函数从 protected 移到 public，否则 com_bag/com_router 无法调用
    bool publishRaw(const void* data, size_t size, MessageType type = static_cast<MessageType>(0));
    void* loanRaw(size_t size);
    
    // 3. 为 MessageType 增加默认值，兼容只传 2 个参数的老代码
    bool publishLoaned(void* data_ptr, size_t size, MessageType type = static_cast<MessageType>(0));

protected:
    std::string topic_name_;
    QoSProfile qos_;
    TopicShm* shm_ptr_;
};

template <typename T>
class Publisher : public PublisherBase {
public:
    Publisher(const std::string& topic_name, 
              const QoSProfile& qos = QoSProfile::Default())
        : PublisherBase(topic_name, qos) {}

    bool publish(const T& msg) {
        return this->publishRaw(&msg, sizeof(T), static_cast<MessageType>(0)); 
    }

    T* loan() {
        void* ptr = this->loanRaw(sizeof(T));
        return static_cast<T*>(ptr);
    }

    bool publishLoaned(T* data_ptr, size_t size = sizeof(T)) {
        // 4. 【修复手误】确保调用的是 publishLoaned 而不是带 Base 后缀的旧名
        return this->PublisherBase::publishLoaned(data_ptr, size, static_cast<MessageType>(0));
    }
};

} // namespace com_ipc