#include "subscriber.h"

// ==================== Subscriber ====================

Subscriber::Subscriber(const std::string& topic_name) : topic_name_(topic_name), last_seq_(0) {
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
        pthread_mutex_unlock(&shm_ptr_->mutex);
    }
}

Subscriber::~Subscriber() {
    if (shm_ptr_) {
        SystemManager::instance()->lockRobust(&shm_ptr_->mutex);
        if (shm_ptr_->active_subscribers > 0) shm_ptr_->active_subscribers--;
        pthread_mutex_unlock(&shm_ptr_->mutex);
        munmap(shm_ptr_, sizeof(TopicShm));
    }
}

int Subscriber::receiveRaw(void* buffer, size_t buffer_size, int timeout_ms) {
    if (!shm_ptr_ || g_shutdown_requested) return -1;

    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

    // 【重要】：使用 while 防止条件变量“虚假唤醒(Spurious Wakeup)”
    // 只有当全局 seq 大于本地记录的 last_seq_ 时，才说明真有新消息
    while (last_seq_ >= shm_ptr_->seq && !g_shutdown_requested) {
        if (timeout_ms == 0) {
            // 0 表示非阻塞，立刻返回
            pthread_mutex_unlock(&shm_ptr_->mutex);
            return -1;
        } else if (timeout_ms > 0) {
            // >0 表示带超时的条件变量等待
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
        } else {
            // <0 表示死等
            pthread_cond_wait(&shm_ptr_->cond_new_msg, &shm_ptr_->mutex);
        }
    }

    if (g_shutdown_requested) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return -1;
    }

// 走到这里说明一定有新数据。读取队列里存放的 Header
    int slot = (shm_ptr_->write_index - 1) % BUFFER_SIZE;
    
    MessageHeader temp_header = shm_ptr_->buffer[slot].header;
    size_t data_size = temp_header.data_size;
    uint32_t offset = temp_header.pool_offset; // 获取取件码
    
    // 【核心性能优化】：拿到取件码就立刻解锁队列！不耽误其他进程发布数据
    pthread_mutex_unlock(&shm_ptr_->mutex);

    size_t total_size = sizeof(MessageHeader) + data_size;
    if (total_size <= buffer_size) {
        // 【去内存池提货】
        void* source_ptr = MemoryPool::instance()->getPointer(offset);
        std::memcpy(buffer, &temp_header, sizeof(MessageHeader));
        std::memcpy(static_cast<char*>(buffer) + sizeof(MessageHeader), source_ptr, data_size);
        last_seq_ = temp_header.seq;
        return static_cast<int>(total_size);
    }
    
    return -1;
}

bool Subscriber::receiveLoaned(LoanedMessage& msg, int timeout_ms) {
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

    int slot = (shm_ptr_->write_index - 1) % BUFFER_SIZE;
    MessageHeader temp_header = shm_ptr_->buffer[slot].header;
    
    // 拿到取件码立刻解锁，绝不阻塞发布者
    pthread_mutex_unlock(&shm_ptr_->mutex);

    // 【核心零拷贝】：不去 memcpy，直接把内存池的绝对指针甩给用户！
    msg.data = MemoryPool::instance()->getPointer(temp_header.pool_offset);
    msg.size = temp_header.data_size;
    msg.seq = temp_header.seq;
    
    last_seq_ = temp_header.seq;
    return true;
}