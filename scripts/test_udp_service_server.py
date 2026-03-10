import sys
import time

sys.path.append('../build')
import com_ipc_py

# 监听所有网卡，开启 10001 专属服务端口
server = com_ipc_py.UDPServiceServer("0.0.0.0", 10001)
print("🛡️ [RPC 服务端] 跨网微服务已启动，监听端口 10001...")

try:
    while True:
        # 🌟 核心魔法：阻塞等待请求。此时 GIL 已在底层释放，系统极度省电且不会卡死！
        result = server.receive_request()
        
        if result is not None:
            # 优雅的解包：纯净数据 + 跨网身份黑盒凭证
            req_data, client_info = result
            req_str = req_data.decode('utf-8')
            print(f"📥 收到请求: '{req_str}'")
            
            # 模拟真实的硬件调用或 AI 推理耗时 (100毫秒)
            time.sleep(0.1) 
            
            # 组装响应数据
            resp_str = f"✅ [执行完毕] 硬件状态正常，指令 '{req_str}' 已确认"
            print(f"📤 发送响应: '{resp_str}'")
            
            # 🎯 精准制导：带着身份凭证，把结果顺着网线原路轰回去
            server.send_response(resp_str.encode('utf-8'), client_info)
            
except KeyboardInterrupt:
    print("\n🛑 服务端安全关闭。")