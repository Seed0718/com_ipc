import sys
import time
import struct

sys.path.append('../build')
import com_ipc_py

# 监听所有网卡
sub = com_ipc_py.UDPSubscriber("0.0.0.0", 9999)
print("📡 [底盘控制节点] QoS 防乱序引擎已启动，严阵以待！...")

# 🌟 QoS 核心：24 字节包头 (I: uint32_t 序列号)
expected_size = struct.calcsize('=IQffi')
last_seq_id = 0 # 记录上一次成功执行的指令序号

try:
    while True:
        data = sub.receive()
        
        if len(data) == expected_size:
            seq_id, timestamp, speed, steering, gear = struct.unpack('=IQffi', data)
            
            # ==========================================
            # 🛡️ QoS 拦截网：判断时间线是否倒流
            # ==========================================
            if seq_id <= last_seq_id:
                print(f"   ❌ [QoS 拦截] 幽灵包！收到过期指令 (Seq: {seq_id} <= 当前: {last_seq_id})，直接丢弃！")
                continue 
                
            # 判断是否发生了真实的物理丢包
            if last_seq_id != 0 and seq_id > last_seq_id + 1:
                lost_count = seq_id - last_seq_id - 1
                print(f"   ⚠️ [网络抖动] 发生物理丢包！丢失了 {lost_count} 个指令 (Seq: {last_seq_id+1} ~ {seq_id-1})")
                
            # 只有时间线向前的正常包，才能驱动底盘
            last_seq_id = seq_id
            
            # 过滤一下正常打印频率，让屏幕清爽一点 (只打印个位数为0的速度)
            if int(speed * 10) % 10 == 0:
                print(f"✅ [正常执行] Seq: {seq_id:04d} | 目标车速: {speed:.1f} m/s")
            
except KeyboardInterrupt:
    print("\n安全退出 QoS 监听。")