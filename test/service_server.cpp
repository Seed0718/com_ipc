#include "com_ipc.h"
#include <iostream>
#include <cstring>

struct AddRequest {
    int a;
    int b;
};

struct AddResponse {
    int sum;
};

bool add_callback(const void* req, size_t req_size, void* resp, size_t& resp_size) {
    if (req_size != sizeof(AddRequest)) return false;
    const AddRequest* ar = static_cast<const AddRequest*>(req);
    AddResponse* rs = static_cast<AddResponse*>(resp);
    rs->sum = ar->a + ar->b;
    resp_size = sizeof(AddResponse);
    std::cout << "Service: " << ar->a << " + " << ar->b << " = " << rs->sum << std::endl;
    return true;
}

int main() {
    SystemManager::instance();
    ServiceServer server("add", add_callback);
    server.startAsync();  // 后台线程处理请求

    std::cout << "Service server running. Press Enter to stop." << std::endl;
    std::cin.get();
    return 0;
}