#!/bin/bash

# 检查是否提供了 tableid 参数
if [ -z "$1" ]; then
    echo "Usage: $0 <tableid>"
    exit 1
fi

TABLEID="$1"
TARGET_DIR="/opt/og/openGauss-server/sse_storage"

# 直接操作（无需切换用户）
cd "$TARGET_DIR" || exit 1

# 定义文件列表
FILES=('closefb_status.txt' 'closefb_storekey.txt')

# 遍历创建文件并设置权限
for FILE in "${FILES[@]}"; do
    NEW_FILE="${TABLEID}${FILE}"
    touch "$NEW_FILE"
    chmod 777 "$NEW_FILE"
done

echo "Files created successfully for tableid: ${TABLEID}."