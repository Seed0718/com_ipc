#include "action_client.h"

// ==================== ActionClient ====================

ActionClient::ActionClient(const std::string& action_name)
    : action_name_(action_name), next_goal_id_(0), running_(true) {
    // ROS 典型的 Action 三大核心 Topic + 1个Cancel Service
    std::string goal_topic = action_name + "/goal";
    std::string feedback_topic = action_name + "/feedback";
    std::string result_topic = action_name + "/result";
    std::string cancel_service = action_name + "/cancel";

    goal_pub_ = new Publisher(goal_topic);
    feedback_sub_ = new Subscriber(feedback_topic);
    result_sub_ = new Subscriber(result_topic);
    cancel_client_ = new ServiceClient(cancel_service);

    // 开启独立线程接收并处理回调
    feedback_thread_ = std::thread(&ActionClient::feedbackLoop, this);
    result_thread_ = std::thread(&ActionClient::resultLoop, this);
}

ActionClient::~ActionClient() {
    running_ = false;
    if (feedback_thread_.joinable()) feedback_thread_.join();
    if (result_thread_.joinable()) result_thread_.join();

    delete goal_pub_;
    delete feedback_sub_;
    delete result_sub_;
    delete cancel_client_;
}

// 独立的 Feedback 接收循环
void ActionClient::feedbackLoop() {
    char buffer[sizeof(MessageHeader) + MAX_MSG_SIZE];
    while (running_) {
        // 使用 100ms 超时的 receiveRaw 以便响应 running_ 的变化
        int ret = feedback_sub_->receiveRaw(buffer, sizeof(buffer), 100);
        if (ret > 0) {
            MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buffer);
            if (hdr->type == MSG_CUSTOM && hdr->data_size >= (sizeof(ActionFeedbackHeader) - MAX_MSG_SIZE)) {
                ActionFeedbackHeader fb;
                size_t fixed_size = sizeof(ActionFeedbackHeader) - MAX_MSG_SIZE;
                std::memcpy(&fb, buffer + sizeof(MessageHeader), fixed_size);
                size_t data_size = hdr->data_size - fixed_size;
                if (data_size <= MAX_MSG_SIZE) {
                    std::memcpy(fb.data, buffer + sizeof(MessageHeader) + fixed_size, data_size);
                    std::lock_guard<std::mutex> lock(mutex_);
                    feedbacks_[fb.goal_id] = fb; // 将收到的反馈按 goal_id 更新到字典
                }
            }
        }
    }
}

bool ActionClient::isFinished(int goal_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return results_.find(goal_id) != results_.end();
}

bool ActionClient::waitForResult(int goal_id, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = results_.find(goal_id);
    if (it != results_.end()) return true;

    // 使用 C++ 标准库条件变量挂起当前线程，等待 resultLoop 收到对应数据后通知
    if (timeout_ms <= 0) {
        cv_.wait(lock, [this, goal_id] { return results_.find(goal_id) != results_.end(); });
        return true;
    } else {
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [this, goal_id] { return results_.find(goal_id) != results_.end(); });
    }
}

bool ActionClient::cancelGoal(int goal_id) {
    CancelGoalRequest req;
    req.goal_id = goal_id;
    CancelGoalResponse resp;
    // 底层调用 ServiceClient 触发目标取消操作
    return cancel_client_->call(req, resp, 3000) && resp.success;
}

// 独立的 Result 接收循环
void ActionClient::resultLoop() {
    char buffer[sizeof(MessageHeader) + MAX_MSG_SIZE];
    while (running_) {
        int ret = result_sub_->receiveRaw(buffer, sizeof(buffer), 100);
        if (ret > 0) {
            MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buffer);
            if (hdr->type == MSG_CUSTOM && hdr->data_size >= (sizeof(ActionResultHeader) - MAX_MSG_SIZE)) {
                ActionResultHeader rs;
                size_t fixed_size = sizeof(ActionResultHeader) - MAX_MSG_SIZE;
                std::memcpy(&rs, buffer + sizeof(MessageHeader), fixed_size);
                size_t data_size = hdr->data_size - fixed_size;
                if (data_size <= MAX_MSG_SIZE) {
                    std::memcpy(rs.data, buffer + sizeof(MessageHeader) + fixed_size, data_size);
                    std::lock_guard<std::mutex> lock(mutex_);
                    results_[rs.goal_id] = rs;
                    pending_goals_.erase(rs.goal_id);
                    // 唤醒可能阻塞在 waitForResult 上的线程
                    cv_.notify_all();
                }
            }
        }
    }
}