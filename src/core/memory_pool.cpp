#include "com_ipc/core/memory_pool.h"
#include "com_ipc/core/system_manager.h"
#include <stdexcept>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

MemoryPool* MemoryPool::instance_ = nullptr;

MemoryPool* MemoryPool::instance() {
    if (!instance_) instance_ = new MemoryPool();
    return instance_;
}

MemoryPool::MemoryPool() {
    bool is_first = false;
    // 整个内存大小 = 头信息 + 100MB 数据区
    size_t total_shm_size = sizeof(MemoryPoolShm) + POOL_SIZE;

    shm_fd_ = shm_open(SHM_POOL_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd_ >= 0) {
        is_first = true;
        if (ftruncate(shm_fd_, total_shm_size) == -1) {
            perror("MemoryPool truncate failed");
        }
    } else {
        shm_fd_ = shm_open(SHM_POOL_NAME, O_RDWR, 0666);
    }

    if (shm_fd_ < 0) throw std::runtime_error("Failed to open MemoryPool");

    void* ptr = mmap(NULL, total_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (ptr == MAP_FAILED) throw std::runtime_error("MemoryPool mmap failed");

    shm_ptr_ = static_cast<MemoryPoolShm*>(ptr);
    // 数据区紧跟在 Header 后面
    pool_data_ptr_ = reinterpret_cast<char*>(ptr) + sizeof(MemoryPoolShm);

    if (is_first) {
        // 利用 SystemManager 里的隐藏能力（由于是友元/公有方法可以强行调用，这里简单复用属性）
        // 建议你确保 SystemManager::initRobustMutex 是 public 的，或者手动初始化
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
        pthread_mutex_init(&shm_ptr_->mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        
        shm_ptr_->head_offset = 0;
    }
}

MemoryPool::~MemoryPool() {
    if (shm_ptr_ != MAP_FAILED) {
        munmap(shm_ptr_, sizeof(MemoryPoolShm) + POOL_SIZE);
    }
    if (shm_fd_ >= 0) close(shm_fd_);
}

void MemoryPool::destroy() {
    if (instance_) {
        delete instance_;
        instance_ = nullptr;
    }
}

uint32_t MemoryPool::allocate(size_t size) {
    if (size > POOL_SIZE) throw std::runtime_error("Requested size exceeds pool capacity!");

    // 8 字节对齐，保证 CPU 访存效率
    size = (size + 7) & ~7;

    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);
    
    uint32_t offset = shm_ptr_->head_offset;
    // 环形分配核心逻辑：如果剩余空间不够，直接绕回 0
    if (offset + size > POOL_SIZE) {
        offset = 0;
    }
    
    shm_ptr_->head_offset = offset + size;
    
    pthread_mutex_unlock(&shm_ptr_->mutex);
    return offset;
}

void* MemoryPool::getPointer(uint32_t offset) {
    return pool_data_ptr_ + offset;
}

uint32_t MemoryPool::getOffset(void* ptr) {
    char* p = static_cast<char*>(ptr);
    // 安全校验：确认这个指针真的是我们内存池里的指针
    if (p < pool_data_ptr_ || p >= pool_data_ptr_ + POOL_SIZE) {
        throw std::runtime_error("Zero-Copy Publish Error: Pointer is not from MemoryPool!");
    }
    return static_cast<uint32_t>(p - pool_data_ptr_);
}