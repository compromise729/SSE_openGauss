#!/usr/bin/env python3
"""Read-only preflight for the current Fides counter and table-map formats."""
# 实验前只读核对关键词计数和表列映射，输出 JSON 和处理建议。
import argparse
import json
import os
import re
import stat
import sys

MAX_COUNT = 2147483647  # Signed int on the project's CentOS 7 x86_64 baseline.
SCHEMES = frozenset("undefined fides mitra fbdsse orion dual closefb januspp".split())


# 从最后一个空格拆出计数，保留关键词内部的空格。
def parse_counter(line):
    keyword, sep, count = line.rpartition(" ")
    if not sep or not count:
        raise ValueError("缺少末尾计数或分隔空格")
    if not keyword.strip():
        raise ValueError("关键词为空")
    if re.fullmatch(r"-[0-9]+", count):
        raise ValueError("计数必须为非负整数")
    if not re.fullmatch(r"[0-9]+", count):
        raise ValueError("计数必须全部为十进制数字")
    # 先按字符串检查范围，再转换为整数。
    digits = count.lstrip("0") or "0"
    limit = str(MAX_COUNT)
    if len(digits) > len(limit) or (len(digits) == len(limit) and digits > limit):
        raise ValueError("计数超出当前 int 范围 0..2147483647")
    return keyword, int(digits)


# 读取一行表名及其列映射，检查方案是否合法、列名是否重复。
def parse_mapping(line):
    fields = line.split()
    if len(fields) < 2 or "|" in fields[0]:
        raise ValueError("映射应包含表名和至少一个 列名|方案")
    columns = []
    seen = set()
    for field in fields[1:]:
        column, sep, scheme = field.partition("|")
        if not sep or not column or not scheme or "|" in scheme:
            raise ValueError("列映射应为 列名|方案")
        if scheme not in SCHEMES:
            raise ValueError("列映射包含当前格式未定义的方案")
        if column in seen:
            raise ValueError("同一表记录中列名重复")
        seen.add(column)
        columns.append({"column": column, "scheme": scheme})
    return fields[0], columns


# 逐行只读检查指定文件，找到目标后也继续检查后面的记录。
def inspect(path, target, parse, field):
    result = {"path": path, "target": target}
    count = 0
    value = None
    try:
        # Non-blocking open avoids hanging if a supplied path points to a FIFO.
        with open(path, "rb", opener=lambda name, flags: os.open(
                name, flags | os.O_NONBLOCK)) as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                return dict(result, status="unreadable", reason="不是普通文件")
            for number, raw in enumerate(stream, 1):
                try:
                    if not raw.endswith(b"\n"):
                        raise ValueError("记录缺少结尾换行，可能截断")
                    line = raw[:-1].rstrip(b"\r").decode("utf-8")
                    if "\x00" in line:
                        raise ValueError("记录包含 NUL 字符")
                    key, entry = parse(line)
                except UnicodeDecodeError:
                    return dict(result, status="format_error", line=number,
                                reason="记录不是有效 UTF-8")
                except ValueError as exc:
                    return dict(result, status="format_error", line=number, reason=str(exc))
                count += 1
                # Both baseline loaders give later rows precedence for the same key.
                if key == target:
                    value = entry
    except FileNotFoundError:
        return dict(result, status="missing", reason="文件不存在")
    except OSError as exc:
        return dict(result, status="unreadable", reason=exc.strerror or "文件读取失败")
    if value is None:
        return dict(result, status="unregistered", empty=(count == 0),
                    reason="空文件，目标未登记" if count == 0 else "目标未登记")
    return dict(result, status="found", empty=False, **{field: value})


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--counter-file", required=True)
    ap.add_argument("--mapping-file", required=True)
    ap.add_argument("--table", required=True)
    ap.add_argument("--keyword", required=True)
    args = ap.parse_args()
    counter = inspect(args.counter_file, args.keyword, parse_counter, "count")
    mapping = inspect(args.mapping_file, args.table, parse_mapping, "columns")
    # 文件异常优先于目标未登记，两侧都找到目标才返回 0。
    statuses = {counter["status"], mapping["status"]}
    if statuses & {"missing", "unreadable", "format_error"}:
        code = 3
        advice = "状态异常，先处理文件问题；如预期为首次初始化，请人工核对实验前提。"
    elif "unregistered" in statuses:
        code = 1
        advice = "目标未登记，请核对输入和实验初始状态，再决定是否继续。"
    else:
        code = 0
        advice = "格式和目标记录检查通过；核对两侧状态与数据库后可继续样例。"
    print(json.dumps({"counter": counter, "mapping": mapping,
                      "recommendation": advice}, ensure_ascii=True, sort_keys=True))
    return code


if __name__ == "__main__":
    sys.exit(main())
