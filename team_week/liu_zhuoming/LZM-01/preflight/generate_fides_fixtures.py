#!/usr/bin/env python3
"""Create public fixtures in a new directory or an existing empty directory."""
# 在临时目录生成公开夹具，供检查器测试正常记录和文件异常。
import argparse
from pathlib import Path

COUNTER = "apple 2\ngreen apple 1\n"
MAPPING = "fides_demo ind|undefined keyword|fides payload|undefined\n"
# 每组依次给出计数文件和映射文件的内容，None 表示该文件缺失。
FIXTURES = {
    "normal": (COUNTER, MAPPING),
    "empty": ("", ""),
    "non_numeric": (COUNTER + "bad 2xyz\n", MAPPING),
    "truncated": (COUNTER + "bad ", MAPPING),
    "missing_count": (COUNTER + "bad \n", MAPPING),
    "overflow": ("apple 2147483648\n", MAPPING),
    "negative": ("apple -1\n", MAPPING),
    "bounds": ("apple 0\ngreen apple 2147483647\n", MAPPING),
    "missing_counter": (None, MAPPING),
    "missing_mapping": (COUNTER, None),
    "unreadable": (COUNTER, MAPPING),
    "bad_mapping": (COUNTER, MAPPING + "another keyword:fides\n"),
    "bad_scheme": (COUNTER, "fides_demo keyword|unsupported\n"),
    "duplicate_table": (COUNTER, MAPPING + "fides_demo keyword|fides\n"),
    "bad_encoding": (COUNTER.encode("utf-8") + b"\xff\n", MAPPING),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("directory", type=Path)
    args = ap.parse_args()
    root = args.directory
    try:
        # 只接受新目录或空目录，保护已有文件。
        if root.is_symlink() or (root.exists() and
                                (not root.is_dir() or any(root.iterdir()))):
            ap.error("请选择新建或空目录，不覆盖已有文件")
        root.mkdir(mode=0o700, exist_ok=True)
        for name, pair in FIXTURES.items():
            case = root / name
            case.mkdir()
            for filename, data in zip(("fides_client.mat", "sse_table.mat"), pair):
                if data is None:
                    continue
                with (case / filename).open("xb") as handle:
                    handle.write(data.encode("utf-8") if isinstance(data, str) else data)
        # 撤销读取权限，让普通用户实际遇到读取失败。
        (root / "unreadable" / "fides_client.mat").chmod(0)
        (root / "unreadable" / "sse_table.mat").chmod(0)
    except OSError as exc:
        ap.error(str(exc))
    print(root)


if __name__ == "__main__":
    main()
