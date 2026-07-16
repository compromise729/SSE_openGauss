#!/bin/bash

# 检查是否提供了 tableid 参数
if [ -z "$1" ]; then
    echo "Usage: $0 <tableid>"
    exit 1
fi

TABLEID="$1"
TARGET_DIR="/opt/og/openGauss-server/sse_storage"

# 直接切换到目标目录（无需切换用户）
cd "$TARGET_DIR" || exit 1

# 定义文件列表
FILES=('dual_key.txt' 'dual_cnt.txt' 'dual_ukey.txt' 'dual_ucnt.txt' 'dual_uwind.txt' 'dual_wind.txt' 'dual_storekey.txt')

# 遍历创建文件并设置权限
for FILE in "${FILES[@]}"; do
    NEW_FILE="${TABLEID}${FILE}"
    touch "$NEW_FILE"
    chmod 777 "$NEW_FILE"
done

echo "All files created and permissions set successfully for tableid: ${TABLEID}."