#ifndef SERVICE_CLIENT_H
#define SERVICE_CLIENT_H


#include "com_ipc_types.h"
#include "system_manager.h"

// --- 客户端 (Service Client) ---
class ServiceClient {
public:
    explicit ServiceClient(const std::string& service_name);
    ~ServiceClient();

    // 同步调用服务，带有超时机制
    template<typename TReq, typename TResp>
    bool call(const TReq& request, TResp& response, int timeout_ms = 3000) {
        static_assert(sizeof(TReq) <= MAX_MSG_SIZE, "Request too large");
        static_assert(sizeof(TResp) <= MAX_MSG_SIZE, "Response too large");

        if (!shm_ptr_ || g_shutdown_requested) return false;

        SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

        // 1. 等待通道空闲
        while (shm_ptr_->has_request && !g_shutdown_requested) {
             struct timespec ts;
             clock_gettime(CLOCK_REALTIME, &ts);
             ts.tv_sec += timeout_ms / 1000;
             ts.tv_nsec += (timeout_ms % 1000) * 1000000;
             if (ts.tv_nsec >= 1000000000) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000; }
             
             int ret = pthread_cond_timedwait(&shm_ptr_->cond_response, &shm_ptr_->mutex, &ts);
             // 只要出错（超时、锁持有者死亡等），直接强制解散并报错退出！不会死等！
             if (ret != 0) {
                 shm_ptr_->has_request = false;
                 shm_ptr_->has_response = false;
                 if (ret == EOWNERDEAD) pthread_mutex_consistent(&shm_ptr_->mutex);
                 pthread_mutex_unlock(&shm_ptr_->mutex);
                 return false;
             }
        }

        if (g_shutdown_requested) {
             pthread_mutex_unlock(&shm_ptr_->mutex);
             return false;
        }

        // 2. 发送请求
        shm_ptr_->request.client_pid = getpid();
        shm_ptr_->request.request_id = ++next_req_id_;
        shm_ptr_->request.data_size = sizeof(TReq);
        std::memcpy(shm_ptr_->request.data, &request, sizeof(TReq));
        
        shm_ptr_->has_request = true;
        shm_ptr_->has_response = false;
        
        pthread_cond_broadcast(&shm_ptr_->cond_request);

        // 3. 等待响应
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000; }

        while (!shm_ptr_->has_response && !g_shutdown_requested) {
            int ret = pthread_cond_timedwait(&shm_ptr_->cond_response, &shm_ptr_->mutex, &ts);
            // 同上，防死等
            if (ret != 0) {
                shm_ptr_->has_request = false;
                shm_ptr_->has_response = false;
                if (ret == EOWNERDEAD) pthread_mutex_consistent(&shm_ptr_->mutex);
                pthread_mutex_unlock(&shm_ptr_->mutex);
                return false;
            }
        }

        if (g_shutdown_requested || shm_ptr_->response.request_id != next_req_id_ || shm_ptr_->response.data_size != sizeof(TResp)) {
            pthread_mutex_unlock(&shm_ptr_->mutex);
            return false;
        }

        std::memcpy(&response, shm_ptr_->response.data, sizeof(TResp));
        shm_ptr_->has_response = false;
        pthread_mutex_unlock(&shm_ptr_->mutex);
        
        return true;
    }

private:
    std::string service_name_;
    ServiceShm* shm_ptr_;
    int next_req_id_ = 0; // 客户端本地请求计数器
};

#endif // SERVICE_CLIENT_H
