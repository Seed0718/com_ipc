#ifndef TOPIC_MANAGER_H
#define TOPIC_MANAGER_H

#include "com_ipc/core/com_ipc_types.h"

// ==================== 类声明 ====================

// 资源管理器：单例模式，负责创建和分发共享内存映射指针
class SystemManager {
public:
    static SystemManager* instance();
    static void destroy(); // 彻底清理 /dev/shm 下的文件
    static void spin(); // 【新增】：阻塞当前线程，直到收到退出信号
    
    TopicShm* createOrGetTopic(const std::string& name, bool is_publisher);
    ServiceShm* createOrGetService(const std::string& name, bool is_server);

    void listTopics();

    // 提供给上层组件使用的鲁棒加锁方法
    bool lockRobust(pthread_mutex_t* mutex);

    // ==================== 节点户籍管理 ====================
    int registerNode(const std::string& node_name);
    void unregisterNode(int node_id);
    void listNodes();
    pid_t getNodePid(const std::string& node_name); // 查询某人的 PID


private:
    SystemManager();
    ~SystemManager();
    static SystemManager* instance_;

    int shm_fd_;
    SystemManagerShm* shm_ptr_;

    // 初始化内核级进程间同步原语
    void initRobustMutex(pthread_mutex_t* mutex);
    void initSharedCond(pthread_cond_t* cond);
};

#endif//TOPIC_MANAGER_H