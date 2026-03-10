import sys
import time
import struct
import threading
import numpy as np

sys.path.append('../build')
import com_ipc_py

# 1. 唤醒 C++ 共享内存大管家
com_ipc_py.init()
node = com_ipc_py.Node("ai_planner_node")

# 2. 建立双通道发射器
pub_shm = node.create_publisher("camera_front")                 # 图像走 SHM 零拷贝
pub_udp = com_ipc_py.UDPPublisher("239.255.0.1", 9999)          # 指令走 UDP 多播

print("🚀 [混合发送端] 视觉(SHM) + 控制(UDP) 双通道已启动！")

speed = 0.0
try:
    while True:
        # ==========================================
        # 🖼️ 通道一：发送 5MB 模拟图像 (SHM 零拷贝)
        # ==========================================
        # 生成一张 1080p 的全黑彩色图像 (约 5.9 MB)
        dummy_image = np.zeros((1080, 1920, 3), dtype=np.uint8)
        pub_shm.publish(dummy_image)

        # ==========================================
        # ⚡ 通道二：发送 20 字节控制指令 (UDP 多播)
        # ==========================================
        speed += 0.5
        if speed > 30.0: speed = 0.0
        timestamp = int(time.time() * 1e6)
        
        cmd_data = struct.pack('=Qffi', timestamp, speed, 0.0, 1)
        pub_udp.publish(cmd_data)

        print(f"👉 发送流 -> 图像: 5.9MB (SHM) | 车速: {speed:.1f} m/s (UDP)")
        
        # 模拟 10Hz 的系统主频
        time.sleep(0.1) 
        
except KeyboardInterrupt:
    print("\n停止广播。")
finally:
    # 优雅退出，释放共享内存池
    com_ipc_py.destroy()