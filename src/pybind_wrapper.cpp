#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include "com_ipc.h"
#include "udp_node.h"

namespace py = pybind11;

// 定义 Python 模块，名字叫 com_ipc_py
PYBIND11_MODULE(com_ipc_py, m) {
    m.doc() = "Zero-copy IPC framework Python bindings";

    // 1. 暴露 SystemManager (大管家)
    m.def("init", []() { SystemManager::instance(); }, "Initialize the IPC system");
    m.def("spin", []() {
        // 在 spin 阻塞时释放 Python 的 GIL 锁，否则 Python 主线程会卡死无法响应 Ctrl+C
        py::gil_scoped_release release; 
        SystemManager::spin();
    }, "Spin the event loop");
    m.def("destroy", []() { SystemManager::destroy(); }, "Destroy the IPC system");

    // 2. 暴露 Publisher (发布者)
    py::class_<Publisher>(m, "Publisher")
        // Python 端传入 bytes 或 numpy array (统一抽象为 py::buffer)
        .def("publish", [](Publisher& self, py::buffer b) {
            py::buffer_info info = b.request();
            // 向共享内存池借用空间
            void* ptr = self.loanRaw(info.size);
            if (ptr) {
                // 将 Python 传来的数据拷贝进共享内存池
                std::memcpy(ptr, info.ptr, info.size);
                self.publishLoaned(ptr, info.size);
            } else {
                throw std::runtime_error("Failed to loan memory from SHM pool!");
            }
        }, "Publish a bytes-like object or numpy array");

    // 3. 暴露 Node (节点大管家)
    py::class_<Node>(m, "Node")
        .def(py::init<const std::string&>())
        // 创建发布者，返回引用，生命周期由 Node 管理
        .def("create_publisher", &Node::createPublisher, py::return_value_policy::reference)
        // 创建订阅者：这里的回调函数是真正的“黑魔法”
        .def("create_subscriber", [](Node& self, const std::string& topic, py::function py_cb) {
            self.createSubscriber(topic, [py_cb](const Subscriber::LoanedMessage& msg) {
                // 【核心警告】：C++ 底层收数据的线程是后台线程，要调用 Python 函数必须先获取 GIL 锁！
                py::gil_scoped_acquire acquire;
                
                // 【终极零拷贝】：不要把 C++ 指针拷贝成 Python bytes！
                // 而是直接创建一个 Python 的 memoryview 指向 C++ 的共享内存！
                auto mem_view = py::memoryview::from_memory(
                    msg.data, 
                    msg.size, 
                    true // readonly: 保护共享内存不被 Python 篡改
                );
                
                // 呼叫 Python 回调函数
                py_cb(mem_view);
            });
        }, py::return_value_policy::reference);

    // 绑定 UDP 发送端
    py::class_<com_ipc::UDPPublisher>(m, "UDPPublisher")
        .def(py::init<const std::string&, int>())
        .def("publish", [](com_ipc::UDPPublisher& self, py::bytes data) {
            std::string raw_data = data; 
            return self.publish(raw_data.data(), raw_data.size());
        });

    // 绑定 UDP 接收端
    py::class_<com_ipc::UDPSubscriber>(m, "UDPSubscriber")
        .def(py::init<const std::string&, int>())
        .def("receive", [](com_ipc::UDPSubscriber& self) {
            std::string data = self.receive();
            return py::bytes(data); // 转换回 Python bytes
        });

    // ==========================================
    // 🚀 绑定跨网 RPC 组件 (UDP + QoS)
    // ==========================================

    // 1. 绑定客户端地址小本本 (对 Python 而言是个黑盒，直接传来传去即可)
    py::class_<com_ipc::UDPClientAddress>(m, "UDPClientAddress")
        .def(py::init<>()); 

    // 2. 绑定 UDP Service Client
    py::class_<com_ipc::UDPServiceClient>(m, "UDPServiceClient")
        .def(py::init<const std::string&, int>())
        .def("call", [](com_ipc::UDPServiceClient& self, py::bytes data, int timeout_ms) {
            std::string raw_data = data;
            std::string response;
            
            // 🛡️ 极其重要：在等待网络响应时释放 Python 的 GIL 锁！
            // 这样你的主进程才不会卡死，遇到网络黑洞也能正常被 Ctrl+C 打断
            {
                py::gil_scoped_release release;
                response = self.call(raw_data.data(), raw_data.size(), timeout_ms);
            }
            
            // 如果超时，C++ 底层会返回空字符串，咱们在这转成 Python 的空 bytes
            return py::bytes(response);
        }, py::arg("data"), py::arg("timeout_ms") = 500); // 默认 500ms 超时

    // 3. 绑定 UDP Service Server
    py::class_<com_ipc::UDPServiceServer>(m, "UDPServiceServer")
        .def(py::init<const std::string&, int>())
        .def("receive_request", [](com_ipc::UDPServiceServer& self) -> py::object {
            std::string req_data;
            com_ipc::UDPClientAddress client_info;
            bool success = false;

            // 🛡️ 释放 GIL，让 C++ 底层安静地在网卡上死等，不影响 Python 其他线程
            {
                py::gil_scoped_release release;
                success = self.receive_request(req_data, client_info);
            }

            if (success) {
                // 将纯净的数据和客户端身份凭证打包成 Tuple 返回给 Python
                return py::make_tuple(py::bytes(req_data), client_info);
            }
            return py::none(); // 如果被异常打断，返回 None
        })
        .def("send_response", [](com_ipc::UDPServiceServer& self, py::bytes data, const com_ipc::UDPClientAddress& client_info) {
            std::string raw_data = data;
            return self.send_response(raw_data.data(), raw_data.size(), client_info);
        });
}