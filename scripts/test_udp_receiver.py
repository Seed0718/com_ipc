import sys
import time
import struct

sys.path.append('../build')
import com_ipc_py

# 实例化接收类，死守 127.0.0.1 的 9999 端口
sub = com_ipc_py.UDPSubscriber("127.0.0.1", 9999)
print("📡 [底盘执行节点] UDP 监听已就绪，等待规划指令...")

expected_size = struct.calcsize('=Qffi')

try:
    while True:
        # 阻塞接收，拿到纯正的 bytes 数据
        data = sub.receive()
        
        if len(data) == expected_size:
            # 瞬间解包
            timestamp, speed, steering, gear = struct.unpack('=Qffi', data)
            
            # 计算端到端延迟
            delay_ms = (int(time.time() * 1e6) - timestamp) / 1000.0
            
            print(f"✅ 收到执行指令 -> 车速: {speed:.1f} m/s | 通信延迟: {delay_ms:.2f} ms")
        else:
            print(f"⚠️ 收到异常大小包: {len(data)} 字节")
except KeyboardInterrupt:
    print("\n退出监听。")