import socket
import struct
import time

PORT = 9999

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('127.0.0.1', PORT))

print(f"📡 Python 接收节点已就绪，正在死守 {PORT} 端口...")

struct_fmt = '=Qffi'
expected_size = struct.calcsize(struct_fmt)
print(f"📏 期待的完美包大小: {expected_size} 字节")

try:
    while True:
        data, addr = sock.recvfrom(1024) 
        
        # 完美的严丝合缝！只要 20 字节！
        if len(data) == expected_size: 
            timestamp_us, speed, steering, gear = struct.unpack(struct_fmt, data)
            now_us = int(time.time() * 1e6)
            delay_us = now_us - timestamp_us
            
            if int(speed * 10) % 10 == 0:
                print(f"[{addr[0]}] 收到状态 -> 车速: {speed:.1f} m/s | 挡位: {gear} | 延迟: {delay_us/1000:.2f} ms")
        else:
            # 如果还有意外，这里会立刻抓包
            print(f"❌ 警告：收到异常大小的包 -> {len(data)} 字节")
                
except KeyboardInterrupt:
    print("\n退出监听。")