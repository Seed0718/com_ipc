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
    size_t total_shm_size = sizeof(MemoryPoolShm) + (POOL_BLOCK_SIZE * POOL_BLOCK_COUNT);

    shm_fd_ = shm_open(SHM_POOL_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd_ >= 0) {
        is_first = true;
        ftruncate(shm_fd_, total_shm_size);
    } else {
        shm_fd_ = shm_open(SHM_POOL_NAME, O_RDWR, 0666);
    }

    if (shm_fd_ < 0) throw std::runtime_error("Failed to open MemoryPool");

    void* ptr = mmap(NULL, total_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (ptr == MAP_FAILED) throw std::runtime_error("MemoryPool mmap failed");

    shm_ptr_ = static_cast<MemoryPoolShm*>(ptr);
    pool_data_ptr_ = reinterpret_cast<char*>(ptr) + sizeof(MemoryPoolShm);

    if (is_first) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
        pthread_mutex_init(&shm_ptr_->mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        
        shm_ptr_->search_cursor = 0;
        for (int i = 0; i < POOL_BLOCK_COUNT; ++i) {
            shm_ptr_->ref_counts[i].store(0, std::memory_order_relaxed);
            shm_ptr_->alloc_lengths[i] = 0;
        }
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

// 全新的块扫描分配机制
uint32_t MemoryPool::allocate(size_t size) {
    if (size == 0) return 0;
    // 计算需要多少个连续的 4KB 块
    uint32_t needed_blocks = (size + POOL_BLOCK_SIZE - 1) / POOL_BLOCK_SIZE;
    if (needed_blocks > POOL_BLOCK_COUNT) throw std::runtime_error("Size too large!");

    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

    uint32_t start_idx = shm_ptr_->search_cursor;
    uint32_t count = 0;
    uint32_t found_idx = -1;

    // 扫描内存池寻找连续空闲块
    for (uint32_t i = 0; i < POOL_BLOCK_COUNT; ++i) {
        uint32_t idx = (start_idx + i) % POOL_BLOCK_COUNT;

        // 物理连续性检查：不能越过数组末尾形成环
        if (count == 0 && idx + needed_blocks > POOL_BLOCK_COUNT) continue;

        if (shm_ptr_->ref_counts[idx].load(std::memory_order_relaxed) == 0) {
            count++;
            if (count == needed_blocks) {
                found_idx = idx + 1 - needed_blocks;
                break;
            }
        } else {
            count = 0; // 遇到被占用块，重新计数
        }
    }

    if (found_idx == (uint32_t)-1) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        throw std::runtime_error("MemoryPool exhausted or highly fragmented!");
    }

    // 标记占用：首个 Block 记录引用计数 1（代表创建者/队列持有）
    shm_ptr_->ref_counts[found_idx].store(1, std::memory_order_relaxed);
    shm_ptr_->alloc_lengths[found_idx] = needed_blocks;
    // 附属 Block 标记为 -1，防止被误分配
    for (uint32_t i = 1; i < needed_blocks; ++i) {
        shm_ptr_->ref_counts[found_idx + i].store(-1, std::memory_order_relaxed);
    }

    shm_ptr_->search_cursor = (found_idx + needed_blocks) % POOL_BLOCK_COUNT;
    pthread_mutex_unlock(&shm_ptr_->mutex);

    return found_idx * POOL_BLOCK_SIZE;
}

void* MemoryPool::getPointer(uint32_t offset) {
    return pool_data_ptr_ + offset;
}

// 【无锁】增加引用计数
void MemoryPool::addRef(uint32_t offset) {
    uint32_t idx = offset / POOL_BLOCK_SIZE;
    if (idx < POOL_BLOCK_COUNT && shm_ptr_->ref_counts[idx].load() > 0) {
        shm_ptr_->ref_counts[idx].fetch_add(1, std::memory_order_relaxed);
    }
}

// 【无锁】释放引用计数
void MemoryPool::releaseRef(uint32_t offset) {
    uint32_t idx = offset / POOL_BLOCK_SIZE;
    if (idx >= POOL_BLOCK_COUNT) return;

    // 原子减1，并获取旧值
    int old_ref = shm_ptr_->ref_counts[idx].fetch_sub(1, std::memory_order_acq_rel);
    if (old_ref == 1) { 
        // 旧值是 1，说明现在归 0 了，真正空闲了！连同附属块一起标记为空闲
        uint32_t len = shm_ptr_->alloc_lengths[idx];
        for (uint32_t i = 1; i < len; ++i) {
            shm_ptr_->ref_counts[idx + i].store(0, std::memory_order_relaxed);
        }
    }
}

uint32_t MemoryPool::getOffset(void* ptr) {
    char* p = static_cast<char*>(ptr);
    // 安全校验：确认这个指针真的是我们内存池里的指针
    if (p < pool_data_ptr_ || p >= pool_data_ptr_ + POOL_SIZE) {
        throw std::runtime_error("Zero-Copy Publish Error: Pointer is not from MemoryPool!");
    }
    return static_cast<uint32_t>(p - pool_data_ptr_);
}