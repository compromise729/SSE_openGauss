#!/bin/bash

# 检查是否提供了 tableid 参数
if [ -z "$1" ]; then
    echo "Usage: $0 <tableid>"
    exit 1
fi

TABLEID="$1"
TARGET_DIR="/opt/og/openGauss-server/sse_storage"

# 检查目标目录是否存在
if [ ! -d "$TARGET_DIR" ]; then
    echo "Error: Directory does not exist: $TARGET_DIR"
    exit 1
fi

# 进入目标目录（需 omm 有权限）
cd "$TARGET_DIR" || exit 1

# 定义要创建的文件列表
FILES=(
    "januspp_skhex.txt"
    "januspp_keywordstage.txt"
    "januspp_storekey.txt"
)

# 遍历创建文件并设置权限
for FILE in "${FILES[@]}"; do
    NEW_FILE="${TABLEID}${FILE}"
    touch "$NEW_FILE"
    chmod 777 "$NEW_FILE"
    echo "Created: $NEW_FILE"
done

echo "All files created and permissions set successfully for tableid: ${TABLEID}."