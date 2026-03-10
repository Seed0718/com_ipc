import sys
import time
import struct
import random

sys.path.append('../build')
import com_ipc_py

# 🌟 填入你香橙派的真实局域网 IP
TARGET_IP = "192.168.1.102"
pub_udp = com_ipc_py.UDPPublisher(TARGET_IP, 9999)

print(f"🚀 [AI 规划节点] 启动！准备向 {TARGET_IP} 发射带有序列号的控制流...")

seq_id = 0 
speed = 0.0

try:
    while True:
        seq_id += 1 # 全局序列号，永不回头
        speed += 0.1
        if speed > 30.0: speed = 0.0
        timestamp = int(time.time() * 1e6)
        
        # 正常打包
        cmd_data = struct.pack('=IQffi', seq_id, timestamp, speed, 0.0, 1)
        
        # ==========================================
        # 😈 混沌工程 (Chaos Engineering)：故意制造网络故障
        # ==========================================
        # 1. 模拟网络乱序/重传：有 10% 的概率，突然发一个 2 步之前的旧指令
        if random.random() < 0.1 and seq_id > 2:
            print(f"   😈 [注入故障] 故意发射过期包 (Seq: {seq_id - 2})")
            bad_data = struct.pack('=IQffi', seq_id - 2, timestamp, speed, 0.0, 1)
            pub_udp.publish(bad_data)
            
        # 2. 模拟物理丢包：有 5% 的概率，直接把当前这个正常的包给吞了（不发送）
        if random.random() < 0.05:
            print(f"   🕳️ [注入故障] 故意丢弃当前包 (Seq: {seq_id})")
            # 跳过 publish，直接进入下一次循环
            time.sleep(0.05)
            continue
            
        # 正常发送
        pub_udp.publish(cmd_data)
        
        if int(speed * 10) % 10 == 0:
            print(f"👉 发送正常包 Seq: {seq_id:04d} | 车速: {speed:.1f} m/s")
            
        time.sleep(0.05) # 20Hz 控制频率
        
except KeyboardInterrupt:
    print("\n停止发送。")