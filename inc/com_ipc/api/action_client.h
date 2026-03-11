#pragma once

#include <string>
#include <mutex>
#include <condition_variable>
#include <map>
#include <set>
#include "com_ipc/core/com_ipc_types.h"
#include "com_ipc/api/publisher.h"
#include "com_ipc/api/subscriber.h"
#include "com_ipc/api/service_client.h"
#include "com_ipc/api/action_server.h" 

namespace com_ipc {

template<typename TGoal, typename TFeedback, typename TResult>
class ActionClient {
public:
    ActionClient(const std::string& action_name)
        : action_name_(action_name), next_goal_id_(0) 
    {
        QoSProfile qos = QoSProfile::Default();
        goal_pub_     = new Publisher<ActionGoalMsg<TGoal>>(action_name + "/goal", qos);
        feedback_sub_ = new Subscriber<ActionFeedbackMsg<TFeedback>>(action_name + "/feedback", qos);
        result_sub_   = new Subscriber<ActionResultMsg<TResult>>(action_name + "/result", qos);
        cancel_client_= new ServiceClient<CancelGoalRequest, CancelGoalResponse>(action_name + "/cancel");

        feedback_sub_->registerCallback([this](const ActionFeedbackMsg<TFeedback>* msg) {
            std::lock_guard<std::mutex> lock(mutex_);
            feedbacks_[msg->goal_id] = *msg;
        });

        result_sub_->registerCallback([this](const ActionResultMsg<TResult>* msg) {
            std::lock_guard<std::mutex> lock(mutex_);
            results_[msg->goal_id] = *msg;
            pending_goals_.erase(msg->goal_id);
            cv_.notify_all();
        });
    }

    ~ActionClient() {
        delete goal_pub_; delete feedback_sub_; delete result_sub_; delete cancel_client_;
    }

    int sendGoal(const TGoal& goal) {
        int goal_id = ++next_goal_id_;
        ActionGoalMsg<TGoal> msg{goal_id, goal};
        if (goal_pub_->publish(msg)) {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_goals_.insert(goal_id);
            return goal_id;
        }
        return -1;
    }

    bool isFinished(int goal_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return results_.find(goal_id) != results_.end();
    }

    bool waitForResult(int goal_id, int timeout_ms = 3000) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (results_.find(goal_id) != results_.end()) return true;

        if (timeout_ms <= 0) {
            cv_.wait(lock, [this, goal_id] { return results_.find(goal_id) != results_.end(); });
            return true;
        } else {
            return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                [this, goal_id] { return results_.find(goal_id) != results_.end(); });
        }
    }

    bool getResult(int goal_id, TResult& result, ActionStatus& status) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = results_.find(goal_id);
        if (it == results_.end()) return false;
        
        result = it->second.payload;
        status = static_cast<ActionStatus>(it->second.status);
        results_.erase(it);
        return true;
    }

    bool getFeedback(int goal_id, TFeedback& feedback, ActionStatus& status) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = feedbacks_.find(goal_id);
        if (it == feedbacks_.end()) return false;

        feedback = it->second.payload;
        status = static_cast<ActionStatus>(it->second.status);
        feedbacks_.erase(it);
        return true;
    }

    bool cancelGoal(int goal_id) {
        CancelGoalRequest req{goal_id};
        CancelGoalResponse resp;
        return cancel_client_->call(req, resp, 3000) && resp.success;
    }

private:
    std::string action_name_;
    Publisher<ActionGoalMsg<TGoal>>* goal_pub_;
    Subscriber<ActionFeedbackMsg<TFeedback>>* feedback_sub_;
    Subscriber<ActionResultMsg<TResult>>* result_sub_;
    ServiceClient<CancelGoalRequest, CancelGoalResponse>* cancel_client_;

    int next_goal_id_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::set<int> pending_goals_;
    std::map<int, ActionResultMsg<TResult>> results_;
    std::map<int, ActionFeedbackMsg<TFeedback>> feedbacks_;
};

} // namespace com_ipc