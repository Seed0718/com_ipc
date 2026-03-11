#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include "com_ipc/core/com_ipc_types.h"

class MemoryPool {
public:
    static MemoryPool* instance();
    static void destroy();

    // 申请空间，返回偏移量
    uint32_t allocate(size_t size);
    
    // 根据偏移量获取真实指针
    void* getPointer(uint32_t offset);

    // 根据真实指针，反向计算出偏移量 (取件码)
    uint32_t getOffset(void* ptr);

private:
    MemoryPool();
    ~MemoryPool();
    static MemoryPool* instance_;

    int shm_fd_;
    MemoryPoolShm* shm_ptr_;
    char* pool_data_ptr_; // 真实数据区起始指针
};

#endif // MEMORY_POOL_H