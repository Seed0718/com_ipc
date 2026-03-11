#pragma once

#include <string>
#include <chrono>
#include <unistd.h>
#include "com_ipc/core/com_ipc_types.h"
#include "com_ipc/api/publisher.h"
#include "com_ipc/api/subscriber.h"
#include "com_ipc/api/service_server.h" 

namespace com_ipc {

template<typename TReq, typename TResp>
class ServiceClient {
public:
    explicit ServiceClient(const std::string& service_name)
        : service_name_(service_name), next_req_id_(0) 
    {
        client_pid_ = getpid();
        QoSProfile qos(HistoryPolicy::KEEP_LAST, 10, ReliabilityPolicy::RELIABLE);
        req_pub_  = new Publisher<ServiceRequestMsg<TReq>>(service_name + "_req", qos);
        resp_sub_ = new Subscriber<ServiceResponseMsg<TResp>>(service_name + "_resp", qos);
    }

    ~ServiceClient() {
        delete req_pub_;
        delete resp_sub_;
    }

    bool call(const TReq& request, TResp& response, int timeout_ms = 3000) {
        uint32_t current_req_id = ++next_req_id_;

        ServiceRequestMsg<TReq> req_msg;
        req_msg.client_pid = client_pid_;
        req_msg.request_id = current_req_id;
        req_msg.payload = request;

        if (!req_pub_->publish(req_msg)) return false;

        auto start_time = std::chrono::steady_clock::now();
        while (!g_shutdown_requested) {
            int elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            int remain_ms = timeout_ms - elapsed_ms;
            if (remain_ms <= 0) return false;

            ServiceResponseMsg<TResp>* resp_msg = resp_sub_->loan(remain_ms);
            if (resp_msg) {
                if (resp_msg->client_pid == client_pid_ && resp_msg->request_id == current_req_id) {
                    response = resp_msg->payload;
                    return true;
                }
            }
        }
        return false;
    }

private:
    std::string service_name_;
    pid_t client_pid_;
    uint32_t next_req_id_;
    Publisher<ServiceRequestMsg<TReq>>* req_pub_;
    Subscriber<ServiceResponseMsg<TResp>>* resp_sub_;
};

} // namespace com_ipc