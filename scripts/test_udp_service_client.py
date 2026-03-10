import sys
import time

sys.path.append('../build')
import com_ipc_py

# 🎯 填入你香橙派的物理 IP
TARGET_IP = "192.168.1.102" 
client = com_ipc_py.UDPServiceClient(TARGET_IP, 10001)

print(f"🚀 [RPC 客户端] 准备向 {TARGET_IP}:10001 发起跨网服务调用...")

task_id = 0
try:
    while True:
        task_id += 1
        req_str = f"自检任务_{task_id:03d}"
        print(f"\n👉 正在呼叫服务端: '{req_str}' (等待超时: 500ms)")
        
        start_time = time.time()
        
        # 🌟 核心魔法：一行代码完成 打包、发送、阻塞等待、ID匹配、超时防线！
        response = client.call(req_str.encode('utf-8'), timeout_ms=500)
        
        cost_ms = (time.time() - start_time) * 1000
        
        if response:
            # 如果收到了结果 (底层通过了 Request ID 的严格比对)
            print(f"🎯 调用成功! (耗时: {cost_ms:.2f} ms) | 结果: {response.decode('utf-8')}")
        else:
            # 🚨 QoS 死锁防线被触发！500ms 未回音，底层强制唤醒 Python！
            print(f"⚠️ [QoS 拦截] 调用超时! (网络黑洞或服务端掉线，业务进程免于死锁)")
            
        time.sleep(1) # 每秒发一次请求
        
except KeyboardInterrupt:
    print("\n🛑 客户端安全退出。")