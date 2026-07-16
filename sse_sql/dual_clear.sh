#!/bin/bash

# 检查是否提供了 tableid 参数
if [ -z "$1" ]; then
    echo "Usage: $0 <tableid>"
    exit 1
fi

TABLEID="$1"
TARGET_DIR="/opt/og/openGauss-server/sse_storage"

# 检查目标目录是否存在（直接使用 omm 的权限）
if [ ! -d "$TARGET_DIR" ]; then
    echo "Error: Directory does not exist: $TARGET_DIR"
    exit 1
fi

# 进入目标目录（需 omm 有权限）
cd "$TARGET_DIR" || exit 1

# 定义要删除的文件列表
FILES=("dual_key.txt" "dual_cnt.txt" "dual_ukey.txt" "dual_ucnt.txt" "dual_uwind.txt" "dual_wind.txt" "dual_storekey.txt")

# 遍历删除文件
for FILE in "${FILES[@]}"; do
    TARGET_FILE="${TABLEID}${FILE}"
    if [ -f "$TARGET_FILE" ]; then
        rm -f "$TARGET_FILE"
        echo "Deleted: $TARGET_FILE"
    else
        echo "Not found: $TARGET_FILE"
    fi
done

echo "Cleanup completed for tableid: ${TABLEID}."