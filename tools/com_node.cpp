#include "com_ipc.h"
#include <iostream>
#include <string>
#include <signal.h>

void print_usage() {
    std::cout << "==========================================\n"
              << "  com_node - IPC Node Manager\n"
              << "==========================================\n"
              << "Usage: com_node <command> [node_name]\n\n"
              << "Commands:\n"
              << "  list      List all active nodes in the system\n"
              << "  kill      Gracefully shutdown a node by its name\n"
              << "==========================================\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];

    try {
        SystemManager::instance();

        if (cmd == "list") {
            std::cout << "Active Nodes:\n-------------------------\n";
            SystemManager::instance()->listNodes();
            std::cout << "-------------------------\n";
        } 
        else if (cmd == "kill" && argc == 3) {
            std::string target = argv[2];
            pid_t pid = SystemManager::instance()->getNodePid(target);
            
            if (pid > 0) {
                std::cout << "Sending shutdown signal to Node '" << target 
                          << "' (PID: " << pid << ")...\n";
                // 发送 SIGINT (也就是模拟按下 Ctrl+C) 让其优雅退出
                kill(pid, SIGINT); 
                std::cout << "Node gracefully killed.\n";
            } else {
                std::cout << "Error: Node '" << target << "' not found or already dead.\n";
            }
        } 
        else {
            print_usage();
        }

        SystemManager::destroy();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}