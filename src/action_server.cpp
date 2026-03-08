#include "action_server.h"

// ==================== ActionServer ====================

ActionServer::ActionServer(const std::string& action_name, ExecuteCallback cb)
    : action_name_(action_name), execute_cb_(cb), running_(false) {
    std::string goal_topic = action_name + "/goal";
    std::string feedback_topic = action_name + "/feedback";
    std::string result_topic = action_name + "/result";
    std::string cancel_service = action_name + "/cancel";

    goal_pub_ = new Publisher(goal_topic);
    goal_sub_ = new Subscriber(goal_topic);
    feedback_pub_ = new Publisher(feedback_topic);
    result_pub_ = new Publisher(result_topic);

    // 将 cancel 注册为一个内部 Service
    cancel_srv_ = new ServiceServer(cancel_service,
        [this](const void* req, size_t req_size, void* resp, size_t& resp_size) -> bool {
            if (req_size != sizeof(CancelGoalRequest)) return false;
            const CancelGoalRequest* request = static_cast<const CancelGoalRequest*>(req);
            CancelGoalResponse* response = static_cast<CancelGoalResponse*>(resp);
            // 调用本类中的 cancelCallback 实现逻辑判断
            response->success = cancelCallback(request, response);
            resp_size = sizeof(CancelGoalResponse);
            return true;
        });
}

ActionServer::~ActionServer() {
    shutdown();
    delete goal_pub_; 
    delete goal_sub_;
    delete feedback_pub_;
    delete result_pub_;
    delete cancel_srv_;
}

void ActionServer::start() {
    running_ = true;
    process_thread_ = std::thread(&ActionServer::processLoop, this);
}

void ActionServer::shutdown() {
    running_ = false;
    if (process_thread_.joinable()) process_thread_.join();
}

void ActionServer::goalCallback(const ActionGoalHeader* goal) {
    std::vector<char> data(goal->data, goal->data + goal->data_size);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 将新目标推入处理队列
        goal_queue_[goal->goal_id] = std::move(data);
        active_goals_.insert(goal->goal_id);
    }
}

bool ActionServer::cancelCallback(const CancelGoalRequest* req, CancelGoalResponse* resp) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 如果请求取消的目标正在被处理，则将其加入 preempted_goals 集合
    if (active_goals_.count(req->goal_id)) {
        preempted_goals_.insert(req->goal_id);
        resp->success = true;
        resp->cancelled_goals = 1;
    } else {
        resp->success = false;
        resp->cancelled_goals = 0;
    }
    return true;
}

bool ActionServer::isPreempted(int goal_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // 提供给用户的接口，用户可在耗时计算的大循环中主动调用判断是否被外部打断
    return preempted_goals_.count(goal_id) > 0;
}

void ActionServer::processLoop() {
    char buffer[sizeof(MessageHeader) + MAX_MSG_SIZE];
    while (running_) {
        // 1. 等待并接收目标
        int ret = goal_sub_->receiveRaw(buffer, sizeof(buffer), 100);
        if (ret > 0) {
            MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buffer);
            if (hdr->type == MSG_CUSTOM && hdr->data_size >= (sizeof(ActionGoalHeader) - MAX_MSG_SIZE)) {
                ActionGoalHeader goal;
                std::memcpy(&goal, buffer + sizeof(MessageHeader), hdr->data_size);
                std::vector<char> data(goal.data, goal.data + goal.data_size);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    goal_queue_[goal.goal_id] = std::move(data);
                    active_goals_.insert(goal.goal_id);
                }
            }
        }

        // 2. 取出队列中的首个目标处理
        int goal_id = -1;
        std::vector<char> data;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!goal_queue_.empty()) {
                auto it = goal_queue_.begin();
                goal_id = it->first;
                data = std::move(it->second);
                goal_queue_.erase(it);
            }
        }
        
        // 3. 将取出的目标交给用户注册的回调函数执行
        // 回调函数的第四个参数是 this 指针，允许在用户代码内部调用 server->publishFeedback()
        if (goal_id != -1) {
            execute_cb_(goal_id, data.data(), data.size(), this);
        }
    }
}