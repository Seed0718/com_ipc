#pragma once

#include <string>
#include <functional>
#include <unistd.h>
#include "com_ipc/core/com_ipc_types.h"
#include "com_ipc/api/publisher.h"
#include "com_ipc/api/subscriber.h"

namespace com_ipc {

template<typename TReq>
struct ServiceRequestMsg {
    pid_t client_pid;
    uint32_t request_id;
    TReq payload;
};

template<typename TResp>
struct ServiceResponseMsg {
    pid_t client_pid;
    uint32_t request_id;
    TResp payload;
};

template<typename TReq, typename TResp>
class ServiceServer {
public:
    using ServiceCallback = std::function<bool(const TReq& req, TResp& resp)>;

    ServiceServer(const std::string& service_name, ServiceCallback cb)
        : service_name_(service_name), callback_(cb) 
    {
        QoSProfile qos(HistoryPolicy::KEEP_LAST, 10, ReliabilityPolicy::RELIABLE);
        req_sub_  = new Subscriber<ServiceRequestMsg<TReq>>(service_name + "_req", qos);
        resp_pub_ = new Publisher<ServiceResponseMsg<TResp>>(service_name + "_resp", qos);
    }

    ~ServiceServer() {
        delete req_sub_;
        delete resp_pub_;
    }

    void startAsync() {
        req_sub_->registerCallback([this](const ServiceRequestMsg<TReq>* req_msg) {
            ServiceResponseMsg<TResp> resp_msg;
            resp_msg.client_pid = req_msg->client_pid;
            resp_msg.request_id = req_msg->request_id;
            
            if (callback_(req_msg->payload, resp_msg.payload)) {
                resp_pub_->publish(resp_msg);
            }
        });
    }

private:
    std::string service_name_;
    ServiceCallback callback_;
    Subscriber<ServiceRequestMsg<TReq>>* req_sub_;
    Publisher<ServiceResponseMsg<TResp>>* resp_pub_;
};

} // namespace com_ipc