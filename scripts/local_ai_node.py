import sys
import os
import numpy as np
import time
import threading # 多线程

# 动态把 build 目录加到 Python 的 import 路径里，以便找到 com_ipc_py.xxx.so
build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '../build'))
sys.path.append(build_dir)

import com_ipc_py  # 召唤我们在 C++ 里手搓的底层引擎！

def image_callback(mem_view):
    """
    【核心魔法】：这个 mem_view 是底层的共享内存视图！没有任何拷贝！
    """
    start_time = time.perf_counter()
    
    # 假设我们 C++ 那边发的是 5MB 的 BigImage (前 12 字节是 id, width, height)
    # offset=12 直接跳过 C++ 的头部，瞬间把纯像素映射为 Numpy 数组
    img_tensor = np.frombuffer(mem_view, dtype=np.uint8, offset=12)
    
    # 计算耗时 (毫秒)
    cost = (time.perf_counter() - start_time) * 1000
    
    print(f"[Python AI Node] 收到底层零拷贝共享内存! 大小: {len(mem_view)} 字节")
    print(f" -> Numpy 张量映射耗时: {cost:.4f} 毫秒! 张量维度: {img_tensor.shape}\n")

if __name__ == "__main__":
    com_ipc_py.init()
    node = com_ipc_py.Node("PythonAINode")
    
    print("🚀 Python AI 节点启动！正在直接从 /dev/shm 订阅 'zero_copy_image'...")
    sub = node.create_subscriber("zero_copy_image", image_callback)
    
    # 【终极魔法】：开辟一个后台守护线程，专门伺候 C++ 的事件循环！
    spin_thread = threading.Thread(target=com_ipc_py.spin, daemon=True)
    spin_thread.start()
    
    try:
        print("SystemManager spinning in background... Press Ctrl+C to exit.")
        # Python 主线程在这里轻松摸鱼，专门等你的键盘退出信号
        while True:
            time.sleep(0.1) 
            
    except KeyboardInterrupt:
        print("\n[Python AI Node] 收到 Ctrl+C 退出指令，正在安全关闭内存池...")
    finally:
        # C++ 底层安全释放所有共享内存和锁
        com_ipc_py.destroy()
        print("退出完成！")