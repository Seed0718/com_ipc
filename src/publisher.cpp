#include "publisher.h"

// ==================== Publisher ====================

Publisher::Publisher(const std::string& topic_name) : topic_name_(topic_name) {
    // 强制作为 publisher 创建话题（如果不存在）
    shm_ptr_ = SystemManager::instance()->createOrGetTopic(topic_name, true);
    if (!shm_ptr_) {
        throw std::runtime_error("Publisher failed to create/get topic: " + topic_name);
    }
}

Publisher::~Publisher() {
    // 【核心修复】：只判断是否为空
    if (shm_ptr_) munmap(shm_ptr_, sizeof(TopicShm));
}

bool Publisher::publishRaw(const void* data, size_t size, MessageType type) {
    if (!shm_ptr_ || g_shutdown_requested) return false;

    // 【1. 内存池接管：获取取件码】
    uint32_t offset = MemoryPool::instance()->allocate(size);
    void* target_ptr = MemoryPool::instance()->getPointer(offset);
    std::memcpy(target_ptr, data, size);

    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

    // 【2. 轻量级入队：只存头部和取件码】
    int slot = shm_ptr_->write_index % BUFFER_SIZE;
    shm_ptr_->buffer[slot].header.seq = ++shm_ptr_->seq;
    shm_ptr_->buffer[slot].header.timestamp = time(nullptr);
    shm_ptr_->buffer[slot].header.data_size = size;
    shm_ptr_->buffer[slot].header.type = type;
    shm_ptr_->buffer[slot].header.pool_offset = offset; // 记下取件码！

    shm_ptr_->write_index++;
    pthread_cond_broadcast(&shm_ptr_->cond_new_msg);
    pthread_mutex_unlock(&shm_ptr_->mutex);

    return true;
}

// 零拷贝：借出内存
void* Publisher::loanRaw(size_t size) {
    uint32_t offset = MemoryPool::instance()->allocate(size);
    return MemoryPool::instance()->getPointer(offset);
}

// 零拷贝：发布借出的内存
bool Publisher::publishLoaned(void* data_ptr, size_t size, MessageType type) {
    if (!shm_ptr_ || g_shutdown_requested) return false;

    // 1. 反向算出这个指针对应的“取件码”
    uint32_t offset = MemoryPool::instance()->getOffset(data_ptr);

    // 2. 直接锁定队列，推入头部信息即可（没有任何 memcpy！）
    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

    int slot = shm_ptr_->write_index % BUFFER_SIZE;
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