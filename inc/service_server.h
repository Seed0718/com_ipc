#ifndef SERVICE_SERVER_H
#define SERVICE_SERVER_H

#include "com_ipc_types.h"
#include "system_manager.h"

// --- 服务端 (Service Server) ---
// 回调函数签名：请求数据，请求大小，响应数据指针，引用传递的响应大小
using ServiceCallback = std::function<bool(const void* request, size_t req_size, void* response, size_t& resp_size)>;

class ServiceServer {
public:
    ServiceServer(const std::string& service_name, ServiceCallback cb);
    ~ServiceServer();
    void startAsync();    // 启动后台线程监听请求
    void spinOnce();      // 单次非阻塞检查（供单线程大循环使用）
    void shutdown();

private:
    void serviceLoop();

    std::string service_name_;
    ServiceShm* shm_ptr_;
    ServiceCallback callback_;
    std::thread thread_;
    volatile bool running_;
};

#endif//SERVICE_SERVER_H