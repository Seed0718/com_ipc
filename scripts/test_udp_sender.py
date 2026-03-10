import sys
import time
import struct

# 指向你的动态库所在目录
sys.path.append('../build') 
import com_ipc_py

# 🌟 核心修改：使用真正的多播频道
MULTICAST_IP = "192.168.1.102"
PORT = 9999

# 实例化多播发射器
pub = com_ipc_py.UDPPublisher(MULTICAST_IP, PORT)
print(f"🚀 [上层规划节点] UDP 多播发射器已启动 ({MULTICAST_IP}:{PORT})...")

speed = 0.0
try:
    while True:
        # 模拟速度变化
        speed += 0.5
        if speed > 30.0: speed = 0.0
        
        # 获取微秒时间戳
        timestamp = int(time.time() * 1e6)
        
        # 严格按照 20 字节打包 (uint64, float, float, int)
        data_bytes = struct.pack('=Qffi', timestamp, speed, 0.0, 1)
        
        # 瞬间打入底层网络栈，进行多播
        pub.publish(data_bytes)
        print(f"👉 发送多播指令 -> 目标车速: {speed:.1f} m/s")
        
        # 模拟 10Hz 的控制频率
        time.sleep(0.1) 
except KeyboardInterrupt:
    print("\n退出发送。")