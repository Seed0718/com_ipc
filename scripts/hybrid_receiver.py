import sys
import time
import struct
import threading

sys.path.append('../build')
import com_ipc_py

# 1. 唤醒 C++ 共享内存大管家
com_ipc_py.init()
node = com_ipc_py.Node("chassis_exec_node")

# ==========================================
# 🖼️ 通道一：SHM 异步回调 (处理大吞吐视觉)
# ==========================================
def image_callback(mem_view):
    data_size_mb = len(mem_view) / (1024 * 1024)
    print(f"   [👁️ 视觉通道 SHM] 零拷贝获取图像 -> 大小: {data_size_mb:.2f} MB")

sub_shm = node.create_subscriber("camera_front", image_callback)


# ==========================================
# ⚡ 通道二：UDP 独立线程 (处理高频低延迟指令)
# ==========================================
sub_udp = com_ipc_py.UDPSubscriber("239.255.0.1", 9999)
expected_size = struct.calcsize('=Qffi')

def udp_worker():
    print("   [⚙️ 控制通道 UDP] 物理多播网卡监听已就绪...")
    while True:
        data = sub_udp.receive() 
        if len(data) == expected_size:
            timestamp, speed, steering, gear = struct.unpack('=Qffi', data)
            delay_ms = (int(time.time() * 1e6) - timestamp) / 1000.0
            print(f"   [⚙️ 控制通道 UDP] 收到指令 -> 车速: {speed:.1f} m/s | 极速延迟: {delay_ms:.2f} ms")

udp_thread = threading.Thread(target=udp_worker, daemon=True)
udp_thread.start()


# ==========================================
# 🧠 核心修复：把 C++ 大管家也关进后台守护线程
# ==========================================
def spin_worker():
    com_ipc_py.spin()

# daemon=True 表示当主线程死掉时，这个后台线程也会瞬间被系统干掉
spin_thread = threading.Thread(target=spin_worker, daemon=True)
spin_thread.start()


print("📡 [混合接收端] 双引擎已点火，等待数据洪流...")
try:
    # Python 主线程保持极其轻量的休眠循环，专心倾听 Ctrl+C
    while True:
        time.sleep(0.1)
except KeyboardInterrupt:
    print("\n\n🛑 收到退出指令，正在安全关闭双通道...")
finally:
    # 优雅销毁 C++ 底层系统，释放内存池
    com_ipc_py.destroy()
    print("✅ 资源已清理完毕，安全退出。")