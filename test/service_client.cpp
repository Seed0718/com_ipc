#include "com_ipc.h"
#include <iostream>

struct AddRequest { int a; int b; };
struct AddResponse { int sum; };

int main() {
    SystemManager::instance();
    try {
        ServiceClient client("add");
        AddRequest req{10, 20};
        AddResponse resp;
        if (client.call(req, resp, 5000)) {
            std::cout << "Result: " << resp.sum << std::endl;
        } else {
            std::cerr << "Call failed" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}