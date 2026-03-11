#pragma once
#include <cstddef>

namespace com_ipc {

enum class ReliabilityPolicy { RELIABLE, BEST_EFFORT };
enum class HistoryPolicy { KEEP_LAST, KEEP_ALL };

struct QoSProfile {
    HistoryPolicy history;
    size_t depth;
    ReliabilityPolicy reliability;

    // 显式构造函数，完美兼容 C++11
    QoSProfile(HistoryPolicy h = HistoryPolicy::KEEP_LAST,
               size_t d = 10,
               ReliabilityPolicy r = ReliabilityPolicy::RELIABLE)
        : history(h), depth(d), reliability(r) {}

    static QoSProfile Default() {
        return QoSProfile{};
    }

    static QoSProfile SensorData() {
        return QoSProfile(HistoryPolicy::KEEP_LAST, 5, ReliabilityPolicy::BEST_EFFORT);
    }
};

} // namespace com_ipc