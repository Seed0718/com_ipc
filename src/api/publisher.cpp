#include "com_ipc/api/publisher.h"
#include <iostream>

namespace com_ipc {

PublisherBase::PublisherBase(const std::string& topic_name, const QoSProfile& qos) 
    : topic_name_(topic_name), qos_(qos), shm_ptr_(nullptr) 
{
    shm_ptr_ = SystemManager::instance()->createOrGetTopic(topic_name, true, qos_.depth);
    if (!shm_ptr_) {
        throw std::runtime_error("Publisher failed to create/get topic: " + topic_name);
    }
}

PublisherBase::~PublisherBase() {
    if (shm_ptr_) munmap(shm_ptr_, sizeof(TopicShm));
}

// 确保函数名是 publishRaw
bool PublisherBase::publishRaw(const void* data, size_t size, MessageType type) {
    if (!shm_ptr_ || g_shutdown_requested) return false;
    uint32_t offset = MemoryPool::instance()->allocate(size);
    void* target_ptr = MemoryPool::instance()->getPointer(offset);
    std::memcpy(target_ptr, data, size);

    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);
    // 换成动态 capacity
    int slot = shm_ptr_->write_index % shm_ptr_->capacity;
    // 【新增】：如果这个槽位有旧数据，队列即将覆盖它，因此剥夺队列的引用权
    if (shm_ptr_->write_index >= BUFFER_SIZE) {
        uint32_t old_offset = shm_ptr_->buffer[slot].header.pool_offset;
        MemoryPool::instance()->releaseRef(old_offset);
    }
    shm_ptr_->buffer[slot].header.seq = ++shm_ptr_->seq;
    shm_ptr_->buffer[slot].header.timestamp = time(nullptr);
    shm_ptr_->buffer[slot].header.data_size = size;
    shm_ptr_->buffer[slot].header.type = type;
    shm_ptr_->buffer[slot].header.pool_offset = offset;
    shm_ptr_->write_index++;
    pthread_cond_broadcast(&shm_ptr_->cond_new_msg);
    pthread_mutex_unlock(&shm_ptr_->mutex);
    return true;
}

// 确保函数名是 loanRaw
void* PublisherBase::loanRaw(size_t size) {
    uint32_t offset = MemoryPool::instance()->allocate(size);
    return MemoryPool::instance()->getPointer(offset);
}

// 确保函数名是 publishLoaned
bool PublisherBase::publishLoaned(void* data_ptr, size_t size, MessageType type) {
    if (!shm_ptr_ || g_shutdown_requested) return false;
    uint32_t offset = MemoryPool::instance()->getOffset(data_ptr);
    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);
    int slot = shm_ptr_->write_index % BUFFER_SIZE;
    // 【新增】：如果这个槽位有旧数据，队列即将覆盖它，因此剥夺队列的引用权
    if (shm_ptr_->write_index >= BUFFER_SIZE) {
        uint32_t old_offset = shm_ptr_->buffer[slot].header.pool_offset;
        MemoryPool::instance()->releaseRef(old_offset);
    }
    shm_ptr_->buffer[slot].header.seq = ++shm_ptr_->seq;
    shm_ptr_->buffer[slot].header.timestamp = time(nullptr);
    shm_ptr_->buffer[slot].header.data_size = size;
    shm_ptr_->buffer[slot].header.type = type;
    shm_ptr_->buffer[slot].header.pool_offset = offset;
    shm_ptr_->write_index++;
    pthread_cond_broadcast(&shm_ptr_->cond_new_msg);
    pthread_mutex_unlock(&shm_ptr_->mutex);
    return true;
}

} // namespace com_ipc