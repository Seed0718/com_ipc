#include "system_manager.h"
#include "node.h"
#include <iostream>

Node::Node(const std::string& name) : name_(name) {
    // 【核心新增】：出生时自动登记！
    node_id_ = SystemManager::instance()->registerNode(name_);
    std::cout << "[Node] Created: " << name_ << " (ID: " << node_id_ << ")" << std::endl;
}

Node::~Node() {
    // 【核心新增】：死亡时自动注销！
    SystemManager::instance()->unregisterNode(node_id_);
    std::cout << "[Node] Destroyed: " << name_ << std::endl;
}

Publisher* Node::createPublisher(const std::string& topic_name) {
    auto pub = std::unique_ptr<Publisher>(new Publisher(topic_name));
    Publisher* ptr = pub.get();
    publishers_.push_back(std::move(pub));
    return ptr;
}

Subscriber* Node::createSubscriber(const std::string& topic_name, 
                                   Subscriber::MessageCallback callback) {
    auto sub = std::unique_ptr<Subscriber>(new Subscriber(topic_name));
    Subscriber* ptr = sub.get();
    ptr->registerCallback(callback); // 自动挂载回调函数并启动异步线程
    subscribers_.push_back(std::move(sub));
    return ptr;
}