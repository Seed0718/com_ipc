import sys
import time
import struct
import cv2
import numpy as np
import threading

sys.path.append('../build')
import com_ipc_py

com_ipc_py.init()
node = com_ipc_py.Node("ai_tracker")

# 因为我们在 WSL 本机测试闭环，所以 UDP 目标就设为本机 127.0.0.1
pub_udp = com_ipc_py.UDPPublisher("127.0.0.1", 9999) 

def image_callback(mem_view):
    # 1. 🌟 零拷贝恢复图像矩阵 (必须和发送端尺寸一致: 480高, 640宽, 3通道)
    frame = np.ndarray(buffer=mem_view, dtype=np.uint8, shape=(480, 640, 3))
    
    # 2. --- 极简 AI 视觉追踪逻辑 ---
    # 提取单通道，寻找白色小球的坐标
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    # 找到所有白色像素的 X 坐标
    white_pixels = np.where(gray > 200)
    
    if len(white_pixels[1]) > 0:
        # 计算小球的中心 X 坐标
        center_x = int(np.mean(white_pixels[1]))
        
        # 将 0~640 的坐标，映射为 -1.0(左满舵) 到 1.0(右满舵) 的转向指令
        # 目标是让小球保持在画面正中间 (320)
        error = center_x - 320
        steering = error / 320.0 
    else:
        steering = 0.0 # 没找到小球就直行
        
    # 3. --- 触发 UDP 底盘控制流 ---
    timestamp = int(time.time() * 1e6)
    speed = 2.0 # 假装底盘以 2m/s 前进
    
    # 严丝合缝的 20 字节打包
    cmd_data = struct.pack('=Qffi', timestamp, speed, steering, 1)
    pub_udp.publish(cmd_data)
    
    # 打印看效果 (过滤一下输出频率避免刷屏太快)
    if int(time.time() * 10) % 5 == 0:
        print(f"🧠 [AI 追踪] 发现目标! 坐标 X: {center_x:3d} -> 下发转向指令: {steering:+.2f}")

sub_shm = node.create_subscriber("camera_front", image_callback)
print("🚀 [AI 追踪节点] 视觉监听已就绪，正在开启 C++ 底层事件循环...")

def spin_worker():
    com_ipc_py.spin()

spin_thread = threading.Thread(target=spin_worker, daemon=True)
spin_thread.start()

try:
    while True:
        time.sleep(0.1)
except KeyboardInterrupt:
    print("\n安全关闭 AI 节点...")
finally:
    com_ipc_py.destroy()