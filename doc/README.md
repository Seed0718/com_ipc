# 🚀 com_ipc : 高性能零拷贝跨主机通信中间件

![C++11](https://img.shields.io/badge/C++-11%2B-blue.svg) 
![Linux](https://img.shields.io/badge/OS-Linux%20%7C%20WSL-orange.svg) 
![Python](https://img.shields.io/badge/Python-3.6%2B-green.svg) 
![Arch](https://img.shields.io/badge/Arch-x86%20%7C%20ARM64-lightgrey.svg)

`com_ipc` 是一个专为自动驾驶、机器人和高并发边缘计算场景打造的**超轻量级、极致性能的 IPC（进程间通信）框架**。

它的核心设计对标 ROS 2 DDS 和百度 Apollo Cyber RT，旨在解决海量传感器数据（如高分辨率图像、激光雷达点云）在多进程、多节点乃至跨物理主机环境下的传输瓶颈。

---

## ✨ 核心特性 (Core Features)

* **⚡ 终极的零拷贝传输 (Zero-Copy via Shared Memory)**
  基于 Linux `/dev/shm` 共享内存与定制化的环形内存池（Ring Allocator）。无论负载是 100KB 还是 50MB，发布与订阅的内存拷贝次数始终为 **0**。延迟达到极低水平。
* **🛡️ 进程级死锁自愈 (Robust Mutex)**
  底层采用 `PTHREAD_MUTEX_ROBUST` 机制。即使某个进程在持有锁的瞬间发生崩溃或被强杀，操作系统内核也会自动接管并释放锁，确保系统永不卡死。
* **🧠 事件驱动的单进程多节点 (Event-Driven Executor)**
  完美支持单进程内并发运行成百上千个独立 Node，基于后台线程与条件变量的异步回调机制，实现“控制流触发，数据流共享”。
* **🌐 异构跨网网关 (Distributed Hybrid Network Gateway)**
  内置 `com_router` 守护进程。通过独创的 **“元数据(MsgPack) + 裸载荷(Raw Buffer)”** 混合协议，完美打通 x86 架构（C++）与 ARM 架构（Python）。

---

## 📂 工程目录结构 (Architecture Topology)

```text
com_ipc/
├── CMakeLists.txt      # 现代 CMake 构建脚本
├── deploy.sh           # 🚀 跨机一键部署脚本 (WSL -> ARM)
├── inc/                # 暴露给用户的公共头文件 (com_ipc.h, json.hpp等)
├── src/                # 核心底层实现 (Pub/Sub/Node/SystemManager)
├── test/               # 测试用例 (zero-copy基准测试、多节点测试等)
├── tools/              # 🗡️ 核心工具链源码 (router/bag/topic/node)
├── scripts/            # 🐍 Python 算法接收节点与生态扩展
└── doc/                # 详细架构文档与手册
```

---

## 🛠️ 兵器谱：命令行工具链 (CLI Tools)

编译完成后，`build/bin/` 目录下将生成以下强大的系统级诊断工具：

* `./bin/com_topic [list | hz | bw | echo]` : 探针工具，实时测算话题频率、物理带宽并抓取内存数据。
* `./bin/com_node [list | kill]` : 节点户籍管理，支持跨进程优雅狙击终止节点。
* `./bin/com_bag [record | play]` : 时空回溯工具，零拷贝录制高频传感器数据，并按真实微秒级时间戳精准回放。
* `./bin/com_router [export | import]` : 跨机网关，将本地零拷贝话题导出至局域网，或从网络导入本地。

---

## 💻 快速开始 (Quick Start)

### 1. 环境依赖与一键编译
* Linux 操作系统 (Ubuntu / WSL2 / OrangePi OS)
* GCC / Clang (支持 C++11 及以上)
* CMake 3.10+

```bash
git clone <your_repository_url>
cd com_ipc
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

### 2. 极简 C++ 零拷贝收发范例

**发送端 (Publisher):**
```cpp
#include "com_ipc.h"

int main() {
    SystemManager::instance();
    Node node("SensorNode");
    Publisher* pub = node.createPublisher("camera_data");

    // 零拷贝核心：直接向系统申请 5MB 内存，而不是在用户态 new
    void* ptr = pub->loanRaw(5 * 1024 * 1024); 
    // ... 填充图像数据到 ptr ...
    pub->publishLoaned(ptr, 5 * 1024 * 1024); // 发布！无需任何拷贝
    
    SystemManager::destroy();
    return 0;
}
```

**接收端 (Subscriber):**
```cpp
#include "com_ipc.h"
#include <iostream>

int main() {
    SystemManager::instance();
    Node node("AlgorithmNode");

    // 基于事件驱动的异步回调机制，不阻塞主线程
    node.createSubscriber("camera_data", [](const Subscriber::LoanedMessage& msg) {
        std::cout << "Received Zero-Copy Data! Size: " << msg.size << " bytes.\n";
    });

    SystemManager::spin(); // 挂起主线程，接管后台事件循环
    return 0;
}
```

---

## 🐍 高阶生态：Python AI 算法节点接入

`com_ipc` 通过混合协议（MsgPack Meta + Raw Payload）为 Python 算法节点提供了极速的接入能力。此方案能够以 $O(1)$ 的内存复杂度将底层的海量字节流映射为 Pytorch/OpenCV 可用的张量。

**依赖安装 (香橙派/边缘计算板):**
```bash
pip3 install msgpack numpy
```

**Python 端接收范例 (`scripts/hybrid_receiver.py`):**
```python
import msgpack
import struct
import numpy as np
# ... (Socket 连接代码详见源码) ...

# 1. 解析定长协议头
magic, meta_len, payload_len = struct.unpack('< 4s I I', header_bytes)

# 2. 动态解析 MessagePack 元数据 (生成 Python 字典)
meta = msgpack.unpackb(meta_bytes)
print(f"Topic: {meta['topic']}, Timestamp: {meta['timestamp']}")

# 3. 终极性能：Numpy 零拷贝视图映射 (跳过 C++ 结构体头部 12 字节)
image_matrix = np.frombuffer(payload_bytes, dtype=np.uint8, offset=12)
cv_image = image_matrix.reshape((meta['height'], meta['width'], 3))
```

---

## 🚀 分布式多机自动化部署 (WSL -> ARM)

本项目原生支持跨越 x86 到 ARM 的分布式协作。通过工程根目录下的 `deploy.sh`，可实现代码从开发机 (WSL) 到 边缘算力平台 (OrangePi) 的一键跨架构原生编译部署。

```bash
# 在 WSL 终端执行：
./deploy.sh
```
*(脚本将自动通过 rsync 同步源码，并在远程节点触发 CMake 构建)*

---
*Developed with Hardcore System Programming.*