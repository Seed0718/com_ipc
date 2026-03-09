#ifndef NODE_H
#define NODE_H

#include "publisher.h"
#include "subscriber.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

class Node {
public:
    Node(const std::string& name);
    ~Node();

    std::string getName() const { return name_; }

    // 创建发布者
    Publisher* createPublisher(const std::string& topic_name);
    
    // 创建订阅者（强制使用回调模式，尽显优雅）
    Subscriber* createSubscriber(const std::string& topic_name, 
                                 Subscriber::MessageCallback callback);

private:
    int node_id_;
    std::string name_;
    // 独占智能指针管理，Node 一死，底下的 Pub/Sub 全部自动销毁！
    std::vector<std::unique_ptr<Publisher>> publishers_;
    std::vector<std::unique_ptr<Subscriber>> subscribers_;
};

#endif // NODE_H