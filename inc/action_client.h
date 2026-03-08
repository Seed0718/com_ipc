#ifndef ACTION_CLIENT_H
#define ACTION_CLIENT_H

#include "com_ipc_types.h"
#include "publisher.h"
#include "subscriber.h"
#include "service_client.h"

// --- 动作客户端 (Action Client) ---
class ActionClient {
public:
    ActionClient(const std::string& action_name);
    ~ActionClient();

    // 提交动作目标
    template<typename T>
    int sendGoal(const T& goal) {
        static_assert(sizeof(T) <= MAX_MSG_SIZE, "Goal data too large");
        int goal_id = next_goal_id_++;
        ActionGoalHeader header;
        header.goal_id = goal_id;
        header.timestamp = static_cast<int64_t>(time(nullptr));
        header.data_size = static_cast<uint32_t>(sizeof(T));
        std::memcpy(header.data, &goal, sizeof(T));

        size_t send_size = sizeof(ActionGoalHeader) - MAX_MSG_SIZE + sizeof(T);
        
        if (!goal_pub_->publishRaw(&header, send_size, MSG_CUSTOM)) {
            return -1;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_goals_.insert(goal_id);
        }
        return goal_id;
    }

    bool isFinished(int goal_id);
    bool waitForResult(int goal_id, int timeout_ms = 3000);
    template<typename T>
    bool getResult(int goal_id, T& result, ActionStatus& status);
    template<typename T>
    bool getFeedback(int goal_id, T& feedback, ActionStatus& status);
    bool cancelGoal(int goal_id);

private:
    std::string action_name_;
    Publisher* goal_pub_;
    Subscriber* feedback_sub_;
    Subscriber* result_sub_;
    ServiceClient* cancel_client_;

    int next_goal_id_;
    volatile bool running_;
    std::thread feedback_thread_;
    std::thread result_thread_;

    mutable std::mutex mutex_;
    std::condition_variable cv_; // 本地条件变量，用于 waitForResult
    std::set<int> pending_goals_;
    std::map<int, ActionResultHeader> results_;
    std::map<int, ActionFeedbackHeader> feedbacks_;

    void feedbackLoop();
    void resultLoop();
};

template<typename T>
bool ActionClient::getResult(int goal_id, T& result, ActionStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = results_.find(goal_id);
    if (it == results_.end()) return false;
    if (it->second.data_size != sizeof(T)) return false;
    std::memcpy(&result, it->second.data, sizeof(T));
    status = static_cast<ActionStatus>(it->second.status);
    results_.erase(it);
    pending_goals_.erase(goal_id);
    return true;
}

template<typename T>
bool ActionClient::getFeedback(int goal_id, T& feedback, ActionStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = feedbacks_.find(goal_id);
    if (it == feedbacks_.end()) return false;
    if (it->second.data_size != sizeof(T)) return false;
    std::memcpy(&feedback, it->second.data, sizeof(T));
    status = static_cast<ActionStatus>(it->second.status);
    feedbacks_.erase(it);
    return true;
}


#endif // ACTION_CLIENT_H

