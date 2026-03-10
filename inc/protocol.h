#ifndef COM_IPC_PROTOCOL_H
#define COM_IPC_PROTOCOL_H

#include <cstdint>

namespace com_ipc {

// ==========================================
// 🛡️ 核心网络通信协议规范
// ==========================================

// 强制按 1 字节对齐（极其重要！）
// 防止 C++ 编译器为了内存访问优化而在结构体中间插入空白的 Padding 字节，
// 确保打包发送出去的网络字节流是绝对紧凑的。
#pragma pack(push, 1)

/**
 * @brief 全局 QoS 信封头 (总大小: 14 字节)
 * 所有通过本框架发送的 UDP UDP/多播 数据包，都必须在头部强制携带此信封
 */
struct QoSHeader {
    // 🛡️ QoS 核心字段
    uint32_t seq_id;        // 序列号：每次发包严格递增，用于接收端检测物理丢包和剔除乱序幽灵包
    
    // ⏱️ 扩展字段 (为未来的高级 QoS 预留)
    uint64_t timestamp_us;  // 发送端微秒时间戳：用于接收端计算端到端物理延迟，或实现 Lifespan (生命周期过期丢弃) QoS
    
    // 🏷️ 协议路由字段
    uint8_t  msg_type;      // 消息路由类型：0x00=普通Topic数据, 0x01=Service请求(Req), 0x02=Service响应(Res)
    
    // ⚙️ 策略控制标志位
    uint8_t  qos_flag;      // QoS策略控制：第0位=1表示需要ACK确认(Reliable)，0表示尽力而为(Best Effort)
};

#pragma pack(pop) // 恢复编译器默认的内存对齐方式

} // namespace com_ipc

#endif // COM_IPC_PROTOCOL_H