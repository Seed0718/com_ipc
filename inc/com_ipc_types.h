//这是重构的基石。所有的宏定义（MAX_TOPICS 等）
//内存结构体（MessageHeader, TopicShm, ServiceShm 等）和全局变量声明，全部剪切到这里
#ifndef COM_IPC_TYPES_H
#define COM_IPC_TYPES_H

#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <string>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <set>
#include <map>
#include <vector>
#include <utility>

// 全局退出标志声明
extern volatile sig_atomic_t g_shutdown_requested;

// ==================== 系统硬编码配置 ====================
#define MAX_TOPICS 20          // 系统最大支持的话题数
#define MAX_SUBSCRIBERS 10     // 单个话题最大支持的订阅者数（当前预留，后续可用于高级统计）
#define BUFFER_SIZE 20         // 每个话题的环形缓冲区槽位数
#define MAX_MSG_SIZE 256       // 每条消息的最大载荷字节数
#define MAX_SERVICES 20        // 系统最大支持的服务数

// ==================== POSIX 共享内存文件前缀 ====================
// 这些文件会实际映射在 Linux 的 /dev/shm/ 目录下
#define SHM_MGR_NAME "/ros_ipc_mgr"
#define SHM_TOPIC_PREFIX "/ros_ipc_topic_"
#define SHM_SERVICE_PREFIX "/ros_ipc_service_"

//内存池配置
#define SHM_POOL_NAME "/com_ipc_pool"
#define POOL_SIZE (100 * 1024 * 1024) // 分配 100MB 的超大共享内存池

// ==================== 核心数据结构与枚举 ====================

// 消息类型枚举，用于简单的类型校验
enum MessageType : int32_t {
    MSG_STRING,
    MSG_INT_ARRAY,
    MSG_FLOAT_DATA,
    MSG_POSE,
    MSG_CUSTOM
};

// 动作执行状态
enum ActionStatus : int32_t {
    ACTION_PENDING = 0,
    ACTION_ACTIVE,
    ACTION_SUCCEEDED,
    ACTION_ABORTED,
    ACTION_PREEMPTED
};

// 禁用字节对齐，确保内存布局在不同机器/进程间绝对一致
#pragma pack(push, 1)

// 基础消息头，附带在所有发出的数据之前
struct MessageHeader {
    int32_t type;         // 消息类型
    int32_t seq;          // 序列号（递增）
    int64_t timestamp;    // 时间戳
    uint32_t data_size;   // 实际有效数据大小
    uint32_t pool_offset; // 数据在 100MB 内存池中的偏移量（取件码）
};

// Action 目标请求头部
struct ActionGoalHeader {
    int32_t goal_id;
    int64_t timestamp;
    uint32_t data_size;
    char data[MAX_MSG_SIZE];
};

// Action 状态反馈头部
struct ActionFeedbackHeader {
    int32_t goal_id;
    int32_t status;
    int64_t timestamp;
    uint32_t data_size;
    char data[MAX_MSG_SIZE];
};

// Action 最终结果头部
struct ActionResultHeader {
    int32_t goal_id;
    int32_t status;
    int64_t timestamp;
    uint32_t data_size;
    char data[MAX_MSG_SIZE];
};

// Service 请求头部
struct ServiceRequestHeader {
    pid_t client_pid;     // 发起请求的客户端 PID
    int32_t request_id;   // 客户端生成的请求序列号
    uint32_t data_size;
    char data[MAX_MSG_SIZE];
};

// Service 响应头部
struct ServiceResponseHeader {
    int32_t request_id;   // 对应的请求序列号
    uint32_t data_size;
    char data[MAX_MSG_SIZE];
};

// 取消 Action 目标的请求与响应
struct CancelGoalRequest {
    int32_t goal_id;
};

struct CancelGoalResponse {
    int32_t success;
    int32_t cancelled_goals;
};

#pragma pack(pop)

// ==================== POSIX 共享内存结构定义 ====================

// 话题(Topic)的共享内存布局
struct TopicShm {
    pthread_mutex_t mutex;        // 进程间鲁棒互斥锁，保护整个结构体
    pthread_cond_t cond_new_msg;  // 进程间条件变量，有新消息时广播唤醒所有订阅者

    uint32_t write_index;         // 环形缓冲区写指针
    uint32_t seq;                 // 全局消息序号
    int active_subscribers;       // 活跃订阅者计数

    struct Slot {
            MessageHeader header;
            // 【删除了 char data[MAX_MSG_SIZE];】 现在的队列只存几十字节的 Header！极其轻量！
        } buffer[BUFFER_SIZE];
};

// 话题元信息
struct TopicInfo {
    char name[64];
    bool active;
};

// 服务(Service)的共享内存布局
struct ServiceShm {
    pthread_mutex_t mutex;        // 服务专属鲁棒锁
    pthread_cond_t cond_request;  // 唤醒服务端处理请求
    pthread_cond_t cond_response; // 唤醒客户端接收响应

    bool has_request;             // 标志位：是否有未处理的请求
    bool has_response;            // 标志位：是否已准备好响应

    ServiceRequestHeader request;   // 请求数据区
    ServiceResponseHeader response; // 响应数据区
};

struct ServiceInfo {
    char name[64];
    bool active;
};

// 全局资源管理器(Manager)的共享内存布局
struct SystemManagerShm {
    pthread_mutex_t mutex;        // 管理器鲁棒锁
    int topic_count;              // 当前已注册话题数量
    TopicInfo topics[MAX_TOPICS]; // 话题注册表
    int service_count;            // 当前已注册服务数量
    ServiceInfo services[MAX_SERVICES]; // 服务注册表
};

// 内存池的共享内存头结构
struct MemoryPoolShm {
    pthread_mutex_t mutex; // 内存分配专属锁
    uint32_t head_offset;  // 内存池当前分配游标
};






#endif // COM_IPC_TYPES_H