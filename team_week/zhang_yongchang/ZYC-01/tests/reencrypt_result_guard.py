"""4.2 重插入返回状态检查的离线规则模型。

模拟 libpq 的 PGresult 返回值分类，验证「先检查返回对象是否存在，再判断
返回状态」两级判断以及「失败时停止后续正常数据回查」的控制流。
这是错误分支验证，**不是**数据库运行验证；真实失败必须在独立实例里触发。

落点设计见 ../../02-补丁设计与落点.md。
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class ExecStatus(Enum):
    """libpq ExecStatusType 的建模（取值顺序与 libpq-fe.h 一致，仅用于离线测试）。"""

    PGRES_EMPTY_QUERY = 0
    PGRES_COMMAND_OK = 1
    PGRES_TUPLES_OK = 2
    PGRES_COPY_OUT = 3
    PGRES_COPY_IN = 4
    PGRES_BAD_RESPONSE = 5
    PGRES_NONFATAL_ERROR = 6
    PGRES_FATAL_ERROR = 7
    PGRES_COPY_BOTH = 8
    PGRES_SINGLE_TUPLE = 9


@dataclass(frozen=True)
class FakePGresult:
    status: ExecStatus
    error_message: str = ""
    sqlstate: str | None = None


@dataclass(frozen=True)
class ReinsertOutcome:
    ok: bool
    continue_data_query: bool
    message: str
    status: ExecStatus | None = None


OK_STATUSES = frozenset({ExecStatus.PGRES_COMMAND_OK, ExecStatus.PGRES_TUPLES_OK})


def evaluate_reinsert_result(
    res: FakePGresult | None,
    connection_error: str = "",
) -> ReinsertOutcome:
    """重插入 PQexec 返回后的判定。

    返回 ok=False 时调用方必须停止第三轮 `SELECT * FROM <表>_data WHERE ind IN (...)`
    正常回查，并且不自动重试、不自动恢复状态。
    """
    # 第一级：返回对象是否存在
    if res is None:
        detail = f" ({connection_error})" if connection_error else ""
        return ReinsertOutcome(
            ok=False,
            continue_data_query=False,
            message=f"Fides re-insert failed: no result returned{detail}",
            status=None,
        )

    # 第二级：拿到对象不等于该次 SQL 成功
    if res.status not in OK_STATUSES:
        parts = [f"Fides re-insert failed: status={res.status.name}"]
        if res.sqlstate:
            parts.append(f"SQLSTATE={res.sqlstate}")
        if res.error_message:
            parts.append(res.error_message.strip())
        return ReinsertOutcome(
            ok=False,
            continue_data_query=False,
            message=", ".join(parts),
            status=res.status,
        )

    return ReinsertOutcome(
        ok=True,
        continue_data_query=True,
        message="Fides re-insert succeeded; original data query continues",
        status=res.status,
    )
