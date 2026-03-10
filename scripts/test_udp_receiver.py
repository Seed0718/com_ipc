import sys
import time
import struct

sys.path.append('../build')
import com_ipc_py

# 🌟 核心修改：监听真正的多播频道
MULTICAST_IP = "192.168.1.102"
PORT = 9999

print(f"📡 [接收节点] 正在加入多播群落 {MULTICAST_IP}:{PORT} ...")
# 实例化接收类，加入多播组
sub = com_ipc_py.UDPSubscriber(MULTICAST_IP, PORT)
print("✅ 多播监听已就绪，等待指挥官指令！")

expected_size = struct.calcsize('=Qffi')

try:
    while True:
        # 阻塞接收，拿到纯正的 bytes 数据
        data = sub.receive()
        
        if len(data) == expected_size:
            timestamp, speed, steering, gear = struct.unpack('=Qffi', data)
            delay_ms = (int(time.time() * 1e6) - timestamp) / 1000.0
            print(f"✅ 收到纯净指令 -> 车速: {speed:.1f} m/s | 物理延迟: {delay_ms:.2f} ms")
        elif len(data) > 0:
            # 🌟 加上这句雷达，如果底层剥离错了，我们会立刻知道！
            print(f"❓ 收到异常大小包: {len(data)} 字节 (预期: {expected_size} 字节)")
except KeyboardInterrupt:
    print("\n退出监听。")