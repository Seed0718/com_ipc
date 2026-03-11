#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <functional>
#include "com_ipc/core/com_ipc_types.h"
#include "com_ipc/api/publisher.h"
#include "com_ipc/api/subscriber.h"
#include "com_ipc/api/service_server.h"

namespace com_ipc {

template<typename TGoal>
struct ActionGoalMsg { int goal_id; TGoal payload; };

template<typename TFeedback>
struct ActionFeedbackMsg { int goal_id; int status; TFeedback payload; };

template<typename TResult>
struct ActionResultMsg { int goal_id; int status; TResult payload; };

template<typename TGoal, typename TFeedback, typename TResult>
class ActionServer {
public:
    using ExecuteCallback = std::function<void(int goal_id, const TGoal& goal_data, ActionServer* server)>;

    ActionServer(const std::string& action_name, ExecuteCallback cb)
        : action_name_(action_name), execute_cb_(cb), running_(false) 
    {
        QoSProfile qos = QoSProfile::Default();
        goal_sub_     = new Subscriber<ActionGoalMsg<TGoal>>(action_name + "/goal", qos);
        feedback_pub_ = new Publisher<ActionFeedbackMsg<TFeedback>>(action_name + "/feedback", qos);
        result_pub_   = new Publisher<ActionResultMsg<TResult>>(action_name + "/result", qos);

        cancel_srv_ = new ServiceServer<CancelGoalRequest, CancelGoalResponse>(
            action_name + "/cancel",
            [this](const CancelGoalRequest& req, CancelGoalResponse& resp) -> bool {
                std::lock_guard<std::mutex> lock(mutex_);
                if (active_goals_.count(req.goal_id)) {
                    preempted_goals_.insert(req.goal_id);
                    resp.success = true;
                    resp.cancelled_goals = 1;
                } else {
                    resp.success = false;
                    resp.cancelled_goals = 0;
                }
                return true;
            });

        goal_sub_->registerCallback([this](const ActionGoalMsg<TGoal>* msg) {
            std::lock_guard<std::mutex> lock(mutex_);
            goal_queue_[msg->goal_id] = msg->payload;
            active_goals_.insert(msg->goal_id);
        });
    }

    ~ActionServer() {
        shutdown();
        delete goal_sub_; delete feedback_pub_; delete result_pub_; delete cancel_srv_;
    }

    void start() {
        running_ = true;
        process_thread_ = std::thread(&ActionServer::processLoop, this);
        cancel_srv_->startAsync();
    }

    void shutdown() {
        running_ = false;
        if (process_thread_.joinable()) process_thread_.join();
    }

    bool publishFeedback(int goal_id, const TFeedback& feedback, ActionStatus status = ACTION_ACTIVE) {
        ActionFeedbackMsg<TFeedback> msg{goal_id, static_cast<int>(status), feedback};
        return feedback_pub_->publish(msg);
    }

    bool publishResult(int goal_id, const TResult& result, ActionStatus status) {
        ActionResultMsg<TResult> msg{goal_id, static_cast<int>(status), result};
        bool ret = result_pub_->publish(msg);
        std::lock_guard<std::mutex> lock(mutex_);
        active_goals_.erase(goal_id);
        return ret;
    }

    bool isPreempted(int goal_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return preempted_goals_.count(goal_id) > 0;
    }

private:
    std::string action_name_;
    Subscriber<ActionGoalMsg<TGoal>>* goal_sub_;
    Publisher<ActionFeedbackMsg<TFeedback>>* feedback_pub_;
    Publisher<ActionResultMsg<TResult>>* result_pub_;
    ServiceServer<CancelGoalRequest, CancelGoalResponse>* cancel_srv_;

    ExecuteCallback execute_cb_;
    std::thread process_thread_;
    volatile bool running_;

    mutable std::mutex mutex_;
    std::map<int, TGoal> goal_queue_;
    std::set<int> active_goals_;
    std::set<int> preempted_goals_;

    void processLoop() {
        while (running_ && !g_shutdown_requested) {
            int goal_id = -1;
            TGoal current_goal;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!goal_queue_.empty()) {
                    auto it = goal_queue_.begin();
                    goal_id = it->first;
                    current_goal = it->second;
                    goal_queue_.erase(it);
                }
            }

            if (goal_id != -1) {
                execute_cb_(goal_id, current_goal, this);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
};

} // namespace com_ipc