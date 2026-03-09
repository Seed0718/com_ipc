#!/bin/bash
TARGET_USER="orangepi"
TARGET_IP="192.168.1.102"
# 【关键修改】：这里改成了 WorkSpace
TARGET_DIR="~/WorkSpace/com_ipc" 

echo "🚀 [1/4] 确保远程香橙派有对应的文件夹..."
ssh $TARGET_USER@$TARGET_IP "mkdir -p $TARGET_DIR"

echo "📦 [2/4] 同步源码到香橙派 (排除 build 目录)..."
# 注意这里用的是 ./ 代表当前 WSL 目录，传到远端的 TARGET_DIR
rsync -avz --exclude='build' ./ $TARGET_USER@$TARGET_IP:$TARGET_DIR/

echo "🔨 [3/4] 在香橙派上触发远程编译..."
ssh $TARGET_USER@$TARGET_IP "cd $TARGET_DIR && mkdir -p build && cd build && cmake .. && cmake --build ."

echo "✅ [4/4] 部署完成！准备起飞！"