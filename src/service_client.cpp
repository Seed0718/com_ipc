#include "service_client.h"
#include "system_manager.h"

// ==================== ServiceClient ====================

ServiceClient::ServiceClient(const std::string& service_name) : service_name_(service_name) {
    int wait_count = 0;
    while (!g_shutdown_requested) {
        shm_ptr_ = SystemManager::instance()->createOrGetService(service_name, false);
        if (shm_ptr_) break;
        
        // 每等 1 秒（循环10次）打印一次警告
        if (++wait_count % 10 == 0) {
            std::cout << "Client waiting for service '" << service_name << "' to be ready..." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

ServiceClient::~ServiceClient() {
    if (shm_ptr_) munmap(shm_ptr_, sizeof(ServiceShm));
}