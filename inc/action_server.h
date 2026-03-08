#ifndef ACTION_SERVER_H
#define ACTION_SERVER_H

#include "com_ipc_types.h"
#include "publisher.h"
#include "subscriber.h"
#include "service_server.h"

// --- 动作服务端 (Action Server) ---
class ActionServer {
public:
    // 回调函数不仅接收数据，还接收 Server 的指针，以便在回调中调用 publishFeedback
    using ExecuteCallback = std::function<void(int goal_id, const void* goal_data, size_t size, ActionServer* server)>;

    ActionServer(const std::string& action_name, ExecuteCallback cb);
    ~ActionServer();
    void start();
    void shutdown();

    template<typename T>
    bool publishFeedback(int goal_id, const T& feedback, ActionStatus status = ACTION_ACTIVE) {
        static_assert(sizeof(T) <= MAX_MSG_SIZE, "Feedback too large");
        ActionFeedbackHeader header;
        header.goal_id = goal_id;
        header.status = static_cast<int32_t>(status);
        header.timestamp = static_cast<int64_t>(time(nullptr));
        header.data_size = static_cast<uint32_t>(sizeof(T));
        std::memcpy(header.data, &feedback, sizeof(T));
        return feedback_pub_->publishRaw(&header, sizeof(ActionFeedbackHeader) - MAX_MSG_SIZE + sizeof(T), MSG_CUSTOM);
    }

    template<typename T>
    bool publishResult(int goal_id, const T& result, ActionStatus status) {
        static_assert(sizeof(T) <= MAX_MSG_SIZE, "Result too large");
        ActionResultHeader header;
        header.goal_id = goal_id;
        header.status = static_cast<int32_t>(status);
        header.timestamp = static_cast<int64_t>(time(nullptr));
        header.data_size = static_cast<uint32_t>(sizeof(T));
        std::memcpy(header.data, &result, sizeof(T));
        return result_pub_->publishRaw(&header, sizeof(ActionResultHeader) - MAX_MSG_SIZE + sizeof(T), MSG_CUSTOM);
    }

    // 用户在回调中可以检查目标是否被客户端请求取消
    bool isPreempted(int goal_id) const;

private:
    std::string action_name_;
    Publisher* goal_pub_;
    Subscriber* goal_sub_;
    Publisher* feedback_pub_;
    Publisher* result_pub_;
    ServiceServer* cancel_srv_;

    ExecuteCallback execute_cb_;
    std::thread process_thread_;
    volatile bool running_;

    mutable std::mutex mutex_;
    std::map<int, std::vector<char>> goal_queue_; // 待处理的目标队列
    std::set<int> active_goals_;
    std::set<int> preempted_goals_;

    void goalCallback(const ActionGoalHeader* goal);
    bool cancelCallback(const CancelGoalRequest* req, CancelGoalResponse* resp);
    void processLoop();
};

#endif//ACTION_SERVER_H