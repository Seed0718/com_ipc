#include "service_server.h"



// ==================== ServiceServer ====================

ServiceServer::ServiceServer(const std::string& service_name, ServiceCallback cb)
    : service_name_(service_name), callback_(cb), running_(false) {
    shm_ptr_ = SystemManager::instance()->createOrGetService(service_name, true);
    if (!shm_ptr_) {
        throw std::runtime_error("ServiceServer failed to create service: " + service_name);
    }
}

ServiceServer::~ServiceServer() {
    shutdown();
    if (shm_ptr_) munmap(shm_ptr_, sizeof(ServiceShm));
}

void ServiceServer::startAsync() {
    if (!running_) {
        running_ = true;
        thread_ = std::thread(&ServiceServer::serviceLoop, this);
    }
}


void ServiceServer::serviceLoop() {
    while (running_) {
        SystemManager::instance()->lockRobust(&shm_ptr_->mutex);
        
        // 阻塞等待新请求
        while (!shm_ptr_->has_request && running_ && !g_shutdown_requested) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            
            int ret = pthread_cond_timedwait(&shm_ptr_->cond_request, &shm_ptr_->mutex, &ts);
            if (ret == EOWNERDEAD) {
                pthread_mutex_consistent(&shm_ptr_->mutex);
                shm_ptr_->has_request = false;
                shm_ptr_->has_response = false;
            } else if (ret != 0 && ret != ETIMEDOUT) {
                break; 
            }
        }

        if (!running_ || g_shutdown_requested) {
            pthread_mutex_unlock(&shm_ptr_->mutex);
            break;
        }

        if (shm_ptr_->has_request && !shm_ptr_->has_response) {
            size_t resp_size = 0;
            ServiceRequestHeader local_req = shm_ptr_->request;
            pthread_mutex_unlock(&shm_ptr_->mutex);

            ServiceResponseHeader local_resp;
            bool success = callback_(local_req.data, local_req.data_size, local_resp.data, resp_size);

            SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

            if (success) {
                local_resp.request_id = local_req.request_id;
                local_resp.data_size = resp_size;
                shm_ptr_->response = local_resp;
                shm_ptr_->has_response = true;
            }
            shm_ptr_->has_request = false;
            pthread_cond_broadcast(&shm_ptr_->cond_response);
        }
        pthread_mutex_unlock(&shm_ptr_->mutex);
    }
}

void ServiceServer::spinOnce() {
    if (!shm_ptr_ || g_shutdown_requested) return;

    SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

    if (shm_ptr_->has_request && !shm_ptr_->has_response) {
        size_t resp_size = 0;
        ServiceRequestHeader local_req = shm_ptr_->request;
        pthread_mutex_unlock(&shm_ptr_->mutex);

        ServiceResponseHeader local_resp;
        bool success = callback_(local_req.data, local_req.data_size, local_resp.data, resp_size);

        SystemManager::instance()->lockRobust(&shm_ptr_->mutex);

        if (success) {
            local_resp.request_id = local_req.request_id;
            local_resp.data_size = resp_size;
            shm_ptr_->response = local_resp;
            shm_ptr_->has_response = true;
        }
        shm_ptr_->has_request = false;
        pthread_cond_broadcast(&shm_ptr_->cond_response);
    }
    pthread_mutex_unlock(&shm_ptr_->mutex);
}

void ServiceServer::shutdown() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}