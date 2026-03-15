#pragma once
#include <cstddef>

namespace com_ipc {

enum class ReliabilityPolicy { RELIABLE, BEST_EFFORT };
enum class HistoryPolicy { KEEP_LAST, KEEP_ALL };

struct QoSProfile {
    HistoryPolicy history;
    size_t depth;
    ReliabilityPolicy reliability;

    // 【新增】：截止时间约束 (单位: 毫秒。0 表示不开启看门狗)
    uint32_t deadline_ms;

    // 显式构造函数，完美兼容 C++11
    QoSProfile(HistoryPolicy h = HistoryPolicy::KEEP_LAST,
               size_t d = 10,
               ReliabilityPolicy r = ReliabilityPolicy::RELIABLE,
               uint32_t deadline = 0)
        : history(h), depth(d), reliability(r),deadline_ms(deadline) {}

    static QoSProfile Default() {
        return QoSProfile{};
    }

    static QoSProfile SensorData() {
        return QoSProfile(HistoryPolicy::KEEP_LAST, 5, ReliabilityPolicy::BEST_EFFORT);
    }

    // 【新增】：专为底盘控制等高频强实时节点提供的预设配置
    static QoSProfile ControlCommand(uint32_t deadline = 100) {
        // 深度设为 1 (只要最新的一帧)，开启看门狗
        return QoSProfile(HistoryPolicy::KEEP_LAST, 1, ReliabilityPolicy::RELIABLE, deadline);
    }

};

} // namespace com_ipc