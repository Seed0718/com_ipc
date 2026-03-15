#include "com_ipc/core/system_manager.h"

volatile sig_atomic_t g_shutdown_requested = 0;

SystemManager* SystemManager::instance_ = nullptr;

// ==================== 核心初始化辅助函数 ====================

// 初始化“鲁棒互斥锁”(Robust Mutex)
// 这是解决由于进程异常崩溃导致死锁的终极方案
void SystemManager::initRobustMutex(pthread_mutex_t* mutex) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    // 设置为跨进程共享，因为这把锁要放在 mmap 的共享内存里
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    // 【关键】设置为鲁棒锁：如果持有锁的进程死亡，操作系统不会让锁死掉
    // 而是会让下一个尝试加锁的进程收到 EOWNERDEAD 返回值
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

// 初始化跨进程条件变量
void SystemManager::initSharedCond(pthread_cond_t* cond) {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    // 设置为跨进程共享，使得一个进程广播，其他阻塞等待的进程都能苏醒
    pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(cond, &attr);
    pthread_condattr_destroy(&attr);
}

// 鲁棒锁加锁封装函数
bool SystemManager::lockRobust(pthread_mutex_t* mutex) {
    int ret = pthread_mutex_lock(mutex);
    if (ret == EOWNERDEAD) {
        // 【核心防御】：捕获到了上一个持有该锁的进程的死亡！
        std::cerr << "[WARNING] Recovering lock from a crashed process!" << std::endl;
        // 调用 pthread_mutex_consistent 修复锁的状态，使其再次可用
        pthread_mutex_consistent(mutex);
        return true;  // 【核心修改】：返回 true 表示发生了崩溃恢复
    } else if (ret != 0) {
        throw std::runtime_error("Failed to lock robust mutex: " + std::string(strerror(ret)));
    }
    return false;
}

// ==================== SystemManager ====================

SystemManager* SystemManager::instance() {
    if (!instance_) {
        instance_ = new SystemManager();
    }
    return instance_;
}

SystemManager::SystemManager() {
    bool is_first_creator = false;
    bool need_wipe = false;
    
    // 1. 尝试以读写模式打开现有的全局管理器文件
    shm_fd_ = shm_open(SHM_MGR_NAME, O_RDWR, 0666);
    if (shm_fd_ >= 0) {
        // 文件存在！此时不能盲目信任，必须检查是否是上一次崩溃遗留的僵尸系统
        SystemManagerShm* temp_ptr = (SystemManagerShm*)mmap(NULL, sizeof(SystemManagerShm), 
                                                             PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
        if (temp_ptr != MAP_FAILED) {
            bool any_alive = false;
            
            // 遍历户口本，看看还有没有活着的进程
            for (int i = 0; i < MAX_NODES; ++i) {
                if (temp_ptr->nodes[i].active) {
                    // kill(pid, 0) 返回 0 表示进程存在，返回 -1 且 errno == ESRCH 表示进程已死
                    if (kill(temp_ptr->nodes[i].pid, 0) == 0) {
                        any_alive = true; // 发现活口！系统还在正常运行中
                        break;
                    }
                }
            }
            
            // 如果名册上有人，但全死光了，说明这是前朝遗迹
            if (!any_alive && temp_ptr->node_count > 0) {
                std::cout << "[SYSTEM MANAGER] 💀 Detected completely dead system state! Initiating auto-wipe..." << std::endl;
                need_wipe = true;
            }
            munmap(temp_ptr, sizeof(SystemManagerShm));
        }
        close(shm_fd_);
    } else {
        // 文件根本不存在，说明这是开天辟地的第一次启动
        is_first_creator = true;
    }

    // ==========================================
    // 🧹 2. 自动清道夫逻辑：模拟 rm -rf /dev/shm/*
    // ==========================================
    if (need_wipe) {
        DIR *dir = opendir("/dev/shm");
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != NULL) {
                // 精准狙击：只删本框架产生的文件，防止误伤系统其他应用的数据
                if (strncmp(ent->d_name, "ros_ipc_", 8) == 0 || 
                    strncmp(ent->d_name, "com_ipc_", 8) == 0) {
                    std::string full_path = std::string("/dev/shm/") + ent->d_name;
                    unlink(full_path.c_str());
                    std::cout << "[WIPE] Deleted orphaned SHM: " << ent->d_name << std::endl;
                }
            }
            closedir(dir);
        }
        // 清理完毕后，当前节点顺理成章成为新的造物主
        is_first_creator = true;
    }

    // ==========================================
    // 🚀 3. 正常的初始化流程
    // ==========================================
    if (is_first_creator) {
        // 独占模式创建全新的文件
        shm_fd_ = shm_open(SHM_MGR_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (shm_fd_ >= 0) {
            if (ftruncate(shm_fd_, sizeof(SystemManagerShm)) == -1) {
                perror("ftruncate mgr");
            }
        } else {
            throw std::runtime_error("Failed to create SystemManager shm after wipe.");
        }
    } else {
        // 加入现有的健康系统
        shm_fd_ = shm_open(SHM_MGR_NAME, O_RDWR, 0666);
        if (shm_fd_ < 0) throw std::runtime_error("Failed to open existing SystemManager shm");
    }

    // 4. 将文件映射到虚拟地址空间
    shm_ptr_ = (SystemManagerShm*)mmap(NULL, sizeof(SystemManagerShm), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (shm_ptr_ == MAP_FAILED) throw std::runtime_error("mmap SystemManager failed");

    // 5. 初始化锁和计数器
    if (is_first_creator) {
        initRobustMutex(&shm_ptr_->mutex);
        shm_ptr_->topic_count = 0;
        shm_ptr_->service_count = 0;
        shm_ptr_->node_count = 0;
        for(int i=0; i<MAX_NODES; ++i) {
            shm_ptr_->nodes[i].active = false;
        }
        std::cout << "[SYSTEM MANAGER] System started fresh." << std::endl;
    }
}

SystemManager::~SystemManager() {
    // 进程退出时，只解除内存映射和关闭文件描述符，不要物理删除文件
    if (shm_ptr_ != MAP_FAILED) munmap(shm_ptr_, sizeof(SystemManagerShm));
    if (shm_fd_ >= 0) close(shm_fd_);
}

void SystemManager::destroy() {
    if (instance_) {
        // 【移除 shm_unlink(SHM_MGR_NAME); 这一行】
        // 绝对不能让某个退出的节点把全局注册表给删了，由系统重启或外部清理脚本负责
        delete instance_;
        instance_ = nullptr;
    }
}

void SystemManager::listTopics() {
    if (!shm_ptr_) return;
    
    lockRobust(&shm_ptr_->mutex);
    std::cout << "\n=== Active Topics (POSIX IPC) ===\n";
    for (int i = 0; i < shm_ptr_->topic_count; ++i) {
        if (shm_ptr_->topics[i].active) {
            std::cout << "  - " << shm_ptr_->topics[i].name << "\n";
        }
    }
    std::cout << "=================================\n";
    pthread_mutex_unlock(&shm_ptr_->mutex);
}


// 获取或创建话题共享内存空间
TopicShm* SystemManager::createOrGetTopic(const std::string& name, bool is_publisher, uint32_t history_depth) {
    std::string shm_name = std::string(SHM_TOPIC_PREFIX) + name;
    for (size_t i = 1; i < shm_name.length(); ++i) {
        if (shm_name[i] == '/') shm_name[i] = '_';
    }
    
    lockRobust(&shm_ptr_->mutex);

    // 1. 查找现有话题
    for (int i = 0; i < shm_ptr_->topic_count; ++i) {
        if (shm_ptr_->topics[i].active && strncmp(shm_ptr_->topics[i].name, name.c_str(), 64) == 0) {
            pthread_mutex_unlock(&shm_ptr_->mutex);
            
            int fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
            if (fd < 0) return nullptr;
            
            // 【核心重构】：利用 fstat 获取这个文件实际的物理大小，而不是瞎猜！
            struct stat st;
            if (fstat(fd, &st) == -1) {
                close(fd);
                return nullptr;
            }
            
            // 按照其实际大小进行动态 mmap 映射
            TopicShm* ptr = (TopicShm*)mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            close(fd);
            
            if (ptr == MAP_FAILED) return nullptr;
            
            if (!is_publisher) {
                lockRobust(&ptr->mutex);
                ptr->active_subscribers++;
                pthread_mutex_unlock(&ptr->mutex);
            }
            return ptr;
        }
    }

    if (!is_publisher) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return nullptr;
    }

    if (shm_ptr_->topic_count >= MAX_TOPICS) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return nullptr;
    }

    int fd = shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) {
        if (errno == EEXIST) {
            shm_unlink(shm_name.c_str());
            fd = shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
        }
        if (fd < 0) {
            pthread_mutex_unlock(&shm_ptr_->mutex);
            return nullptr;
        }
    }

    // 【核心重构】：精确计算创建时所需的总内存大小 = 头部结构体大小 + (深度 * 每个槽位的大小)
    size_t total_shm_size = sizeof(TopicShm) + (history_depth * sizeof(TopicShm::Slot));

    if (ftruncate(fd, total_shm_size) == -1) {
        close(fd);
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return nullptr;
    }

    TopicShm* ptr = (TopicShm*)mmap(NULL, total_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return nullptr;
    }

    initRobustMutex(&ptr->mutex);
    initSharedCond(&ptr->cond_new_msg);
    ptr->write_index = 0;
    ptr->seq = 0;
    ptr->active_subscribers = 0;
    // 【记录真实深度】，下游订阅者就知道该怎么取余数了！
    ptr->capacity = history_depth; 

    int idx = shm_ptr_->topic_count++;
    strncpy(shm_ptr_->topics[idx].name, name.c_str(), 63);
    shm_ptr_->topics[idx].active = true;

    pthread_mutex_unlock(&shm_ptr_->mutex);
    return ptr;
}

// 获取或创建服务共享内存空间（逻辑同上）
ServiceShm* SystemManager::createOrGetService(const std::string& name, bool is_server) {
    std::string shm_name = std::string(SHM_SERVICE_PREFIX) + name;

    for (size_t i = 1; i < shm_name.length(); ++i) {
        if (shm_name[i] == '/') shm_name[i] = '_';
    }
    
    lockRobust(&shm_ptr_->mutex);

    for (int i = 0; i < shm_ptr_->service_count; ++i) {
        if (shm_ptr_->services[i].active && strncmp(shm_ptr_->services[i].name, name.c_str(), 64) == 0) {
            
            if (is_server) {
                // 【终极必杀技】：服务端重启时，无情销毁旧内存，重新分配！
                // 彻底杜绝任何前任崩溃留下的脏锁、脏条件变量和死锁状态
                std::cout << "[INFO] Server restarted. Wiping old service memory...\n";
                shm_unlink(shm_name.c_str());
                int fd = shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
                if (fd >= 0) {
                    if (ftruncate(fd, sizeof(ServiceShm)) == -1) {
                        perror("ftruncate wiped service shm failed");
                        close(fd);
                        pthread_mutex_unlock(&shm_ptr_->mutex);
                        return nullptr;
                    }
                    ServiceShm* ptr = (ServiceShm*)mmap(NULL, sizeof(ServiceShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                    close(fd);
                    if (ptr != MAP_FAILED) {
                        initRobustMutex(&ptr->mutex);
                        initSharedCond(&ptr->cond_request);
                        initSharedCond(&ptr->cond_response);
                        ptr->has_request = false;
                        ptr->has_response = false;
                        pthread_mutex_unlock(&shm_ptr_->mutex);
                        return ptr;
                    }
                }
                pthread_mutex_unlock(&shm_ptr_->mutex);
                return nullptr;
            } else {
                // 客户端正常打开现有的共享内存
                pthread_mutex_unlock(&shm_ptr_->mutex);
                int fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
                if (fd < 0) return nullptr;
                ServiceShm* ptr = (ServiceShm*)mmap(NULL, sizeof(ServiceShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                close(fd);
                if (ptr == MAP_FAILED) return nullptr;
                return ptr;
            }
        }
    }

    if (!is_server) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return nullptr;
    }

    if (shm_ptr_->service_count >= MAX_SERVICES) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        std::cerr << "Max services reached\n";
        return nullptr;
    }

    int fd = shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) {
        if (errno == EEXIST) {
            shm_unlink(shm_name.c_str());
            fd = shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
        }
        if (fd < 0) {
            pthread_mutex_unlock(&shm_ptr_->mutex);
            return nullptr;
        }
    }
    
    if (ftruncate(fd, sizeof(ServiceShm)) == -1) {
        close(fd);
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return nullptr;
    }

    ServiceShm* ptr = (ServiceShm*)mmap(NULL, sizeof(ServiceShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) {
        pthread_mutex_unlock(&shm_ptr_->mutex);
        return nullptr;
    }

    initRobustMutex(&ptr->mutex);
    initSharedCond(&ptr->cond_request);
    initSharedCond(&ptr->cond_response);
    ptr->has_request = false;
    ptr->has_response = false;

    int idx = shm_ptr_->service_count++;
    strncpy(shm_ptr_->services[idx].name, name.c_str(), 63);
    shm_ptr_->services[idx].active = true;

    pthread_mutex_unlock(&shm_ptr_->mutex);
    return ptr;
}

void SystemManager::spin() {
    std::cout << "SystemManager spinning... Press Ctrl+C to exit." << std::endl;
    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 挂起主线程，不占CPU
    }
    std::cout << "SystemManager spin exiting..." << std::endl;
}

// 1. 节点登记，返回分配的户口本号 (node_id)
int SystemManager::registerNode(const std::string& node_name) {
    if (!shm_ptr_) return -1;
    lockRobust(&shm_ptr_->mutex);
    
    int node_id = -1;
    for (int i = 0; i < MAX_NODES; ++i) {
        if (!shm_ptr_->nodes[i].active) { // 找个空位
            strncpy(shm_ptr_->nodes[i].name, node_name.c_str(), 63);
            shm_ptr_->nodes[i].pid = getpid(); // 记下进程的物理地址
            shm_ptr_->nodes[i].active = true;
            node_id = i;
            shm_ptr_->node_count++;
            break;
        }
    }
    
    pthread_mutex_unlock(&shm_ptr_->mutex);
    return node_id;
}

// 2. 节点死亡，注销户口
void SystemManager::unregisterNode(int node_id) {
    if (!shm_ptr_ || node_id < 0 || node_id >= MAX_NODES) return;
    lockRobust(&shm_ptr_->mutex);
    if (shm_ptr_->nodes[node_id].active) {
        shm_ptr_->nodes[node_id].active = false;
        shm_ptr_->node_count--;
    }
    pthread_mutex_unlock(&shm_ptr_->mutex);
}

// 3. 打印花名册
void SystemManager::listNodes() {
    if (!shm_ptr_) return;
    lockRobust(&shm_ptr_->mutex);
    for (int i = 0; i < MAX_NODES; ++i) {
        if (shm_ptr_->nodes[i].active) {
            std::cout << "- " << shm_ptr_->nodes[i].name 
                      << " \t[PID: " << shm_ptr_->nodes[i].pid << "]\n";
        }
    }
    pthread_mutex_unlock(&shm_ptr_->mutex);
}

// 4. 查水表专用（通过名字找 PID）
pid_t SystemManager::getNodePid(const std::string& node_name) {
    if (!shm_ptr_) return -1;
    pid_t target_pid = -1;
    lockRobust(&shm_ptr_->mutex);
    for (int i = 0; i < MAX_NODES; ++i) {
        if (shm_ptr_->nodes[i].active && std::string(shm_ptr_->nodes[i].name) == node_name) {
            target_pid = shm_ptr_->nodes[i].pid;
            break;
        }
    }
    pthread_mutex_unlock(&shm_ptr_->mutex);
    return target_pid;
}