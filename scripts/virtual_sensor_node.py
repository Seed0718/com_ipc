import sys
import time
import cv2
import numpy as np

sys.path.append('../build')
import com_ipc_py

com_ipc_py.init()
node = com_ipc_py.Node("virtual_sensor")
pub_shm = node.create_publisher("camera_front")

print("📷 [虚拟传感器] 启动成功！正在生成动态小球视频流...")

# 画面参数
WIDTH, HEIGHT = 640, 480
ball_x, ball_y = WIDTH // 2, HEIGHT // 2
speed_x = 15 # 小球横向移动速度

try:
    while True:
        # 1. 创建纯黑背景 (模拟 640x480 的彩色图像)
        frame = np.zeros((HEIGHT, WIDTH, 3), dtype=np.uint8)
        
        # 2. 更新小球位置 (碰到边界反弹)
        ball_x += speed_x
        if ball_x <= 30 or ball_x >= WIDTH - 30:
            speed_x = -speed_x # 反弹
            
        # 3. 在画面上画一个白色的实心圆
        cv2.circle(frame, (ball_x, ball_y), 30, (255, 255, 255), -1)
        
        # 4. ⚡ 核心：将这帧画面通过 SHM 零拷贝打出去！
        pub_shm.publish(frame)
        
        # 模拟 30 FPS 的摄像头帧率
        time.sleep(1.0 / 30.0)
        
except KeyboardInterrupt:
    print("\n停止生成虚拟视频流。")
finally:
    com_ipc_py.destroy()