#!/usr/bin/env python3
"""Run Task 2 assertions twice against generated public fixtures as a normal user."""
# 用公开夹具调用检查器，逐项比较预期结果与实际输出。
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
COLUMNS = [{"column": col, "scheme": scheme} for col, scheme in
           [("ind", "undefined"), ("keyword", "fides"), ("payload", "undefined")]]
# fixture, keyword, table, exit, counter assertions, mapping assertions
CASES = [
    ("normal", "apple", "fides_demo", 0, ("found", 2), ("found", COLUMNS)),
    ("empty", "apple", "fides_demo", 1, ("unregistered", True), ("unregistered", True)),
    ("normal", "unlisted", "fides_demo", 1, ("unregistered", False), ("found", COLUMNS)),
    ("normal", "apple", "unlisted", 1, ("found", 2), ("unregistered", False)),
    ("normal", "green apple", "fides_demo", 0, ("found", 1), ("found", COLUMNS)),
    ("non_numeric", "apple", "fides_demo", 3, ("format_error", 3, "十进制"), ("found", COLUMNS)),
    ("truncated", "apple", "fides_demo", 3, ("format_error", 3, "截断"), ("found", COLUMNS)),
    ("missing_count", "apple", "fides_demo", 3, ("format_error", 3, "缺少"), ("found", COLUMNS)),
    ("overflow", "apple", "fides_demo", 3, ("format_error", 1, "超出"), ("found", COLUMNS)),
    ("negative", "apple", "fides_demo", 3, ("format_error", 1, "非负"), ("found", COLUMNS)),
    ("bounds", "apple", "fides_demo", 0, ("found", 0), ("found", COLUMNS)),
    ("bounds", "green apple", "fides_demo", 0, ("found", 2147483647), ("found", COLUMNS)),
    ("missing_counter", "apple", "fides_demo", 3, ("missing",), ("found", COLUMNS)),
    ("missing_mapping", "apple", "fides_demo", 3, ("found", 2), ("missing",)),
    ("unreadable", "apple", "fides_demo", 3, ("unreadable",), ("unreadable",)),
    ("bad_mapping", "apple", "fides_demo", 3, ("found", 2), ("format_error", 2, "列名|方案")),
    ("bad_scheme", "apple", "fides_demo", 3, ("found", 2), ("format_error", 1, "未定义")),
    ("duplicate_table", "apple", "fides_demo", 0, ("found", 2),
     ("found", [{"column": "keyword", "scheme": "fides"}])),
    ("bad_encoding", "apple", "fides_demo", 3, ("format_error", 3, "UTF-8"), ("found", COLUMNS)),
]


# 按结果类别核对计数、映射、空文件标记或错误原因。
def verify(obj, expected, path, target, field):
    status = expected[0]
    if obj["status"] != status or obj["path"] != str(path) or obj["target"] != target:
        raise AssertionError(obj)
    if status == "found" and (obj[field] != expected[1] or obj["empty"] is not False):
        raise AssertionError(obj)
    if status == "unregistered" and obj["empty"] is not expected[1]:
        raise AssertionError(obj)
    if status == "format_error" and (obj["line"] != expected[1] or expected[2] not in obj["reason"]):
        raise AssertionError(obj)
    if status != "found" and (field in obj or not obj["reason"]):
        raise AssertionError(obj)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture_dir", type=Path)
    args = parser.parse_args()
    # 权限用例需要普通用户才能复现读取失败。
    if os.geteuid() == 0:
        parser.error("请用普通用户运行，否则无法验证无读权限用例")
    for number, case in enumerate(CASES, 1):
        fixture, keyword, table, exit_code, expected_c, expected_m = case
        label = "T2-{:02d}".format(number)
        counter = args.fixture_dir / fixture / "fides_client.mat"
        mapping = args.fixture_dir / fixture / "sse_table.mat"
        command = [sys.executable, str(ROOT / "check_fides_state.py"),
                   "--counter-file", str(counter), "--mapping-file", str(mapping),
                   "--table", table, "--keyword", keyword]
        previous = None
        # 同一用例跑两次，核对退出码和 JSON，并比较两次输出。
        for _ in range(2):
            run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
            if run.returncode != exit_code or run.stderr:
                raise AssertionError((label, run.returncode, run.stderr, run.stdout))
            obj = json.loads(run.stdout.decode("ascii"))
            verify(obj["counter"], expected_c, counter, keyword, "count")
            verify(obj["mapping"], expected_m, mapping, table, "columns")
            if not obj["recommendation"] or (previous is not None and previous != run.stdout):
                raise AssertionError((label, "missing advice or inconsistent results"))
            previous = run.stdout
        print(json.dumps({"case": label, "fixture": fixture, "exit": run.returncode,
                          "argv": command, "counter": obj["counter"], "mapping": obj["mapping"],
                          "recommendation": obj["recommendation"],
                          "repeat": 2}, ensure_ascii=True))
    print("PASS: {} cases, {} checker runs".format(len(CASES), len(CASES) * 2))


if __name__ == "__main__":
    main()
