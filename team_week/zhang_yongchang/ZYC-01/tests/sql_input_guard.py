"""4.1 输入检查的离线规则模型。

这不是 openGauss 源码，也不链接 libpq。它只把「先分流、再检查」的判定规则
实现成可测试的纯函数，用来验证分类逻辑本身不会误拦截已有路径。
真实客户端是否拦截，必须在编译安装后的 gsql 上单独验证。

落点设计见 ../../02-补丁设计与落点.md。
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import re
from typing import Iterable


class Origin(Enum):
    USER = "user"
    INTERNAL = "internal"


class Decision(Enum):
    PASS_THROUGH = "PASS_THROUGH"   # 交回原流程：普通 SQL、已有能力、内部请求
    ALLOW_SIMPLE = "ALLOW_SIMPLE"   # 已登记 Fides 表的简单单表关键词查询
    REJECT = "REJECT"               # 本轮未支持的单表输入，执行前拒绝


@dataclass(frozen=True)
class Classification:
    decision: Decision
    reason: str
    table: str | None = None
    keyword: str | None = None
    masked_sql: str | None = None


_STR_PLACEHOLDER = re.compile(r"__ZYC_STR(\d+)__")
_IDENT_PLACEHOLDER = "__ZYC_IDENT0__"


def mask_quoted(sql: str) -> tuple[str, list[str]]:
    """把单引号字符串与双引号标识符替换成不含关键字的占位符。

    单引号内的 '' 与双引号内的 \"\" 按 SQL 转义还原为一个字符。
    返回 (masked_sql, literals)，literals 仅保存单引号字符串内容。
    """
    out: list[str] = []
    literals: list[str] = []
    buf: list[str] = []
    quote: str | None = None
    i = 0
    while i < len(sql):
        ch = sql[i]
        if quote is None:
            if ch in ("'", '"'):
                quote = ch
                buf = []
            else:
                out.append(ch)
            i += 1
            continue
        if ch == quote:
            if i + 1 < len(sql) and sql[i + 1] == quote:
                buf.append(quote)
                i += 2
                continue
            if quote == "'":
                idx = len(literals)
                literals.append("".join(buf))
                out.append(f"__ZYC_STR{idx}__")
            else:
                out.append(_IDENT_PLACEHOLDER)
            quote = None
            i += 1
            continue
        buf.append(ch)
        i += 1
    if quote is not None:
        raise ValueError("unterminated quoted literal")
    return "".join(out), literals


def _table_from_from_clause(from_clause: str) -> str | None:
    """只取 FROM 后的第一个标识符；多表/别名/其它复杂形态返回 None。"""
    m = re.match(r"(?i)^\s*([A-Za-z_][A-Za-z0-9_$]*)", from_clause)
    if not m:
        return None
    table = m.group(1)
    tail = from_clause[m.end():].strip()
    # 允许一个纯别名；其它内容（逗号、ON、JOIN、子查询等）交给原流程
    if tail and not re.fullmatch(r"(?i)(?:AS\s+)?[A-Za-z_][A-Za-z0-9_$]*", tail):
        return None
    return table


def classify(
    sql: str,
    registered_fides_tables: Iterable[str],
    origin: Origin = Origin.USER,
) -> Classification:
    """对用户 SQL 做分类。判定顺序：来源 → 语句类型 → JOIN/多表 → 表名 → 支持形式。"""
    tables = set(registered_fides_tables)

    # 1) 内部请求由调用位置区分，绝不靠 SQL 文本猜
    if origin != Origin.USER:
        return Classification(Decision.PASS_THROUGH, "internal request bypasses user-input guard")

    try:
        masked, literals = mask_quoted(sql)
    except ValueError as exc:
        return Classification(Decision.REJECT, str(exc))

    s = masked.strip().rstrip()
    while s.endswith(";"):
        s = s[:-1].rstrip()

    # 2) 非 SELECT（INSERT/DELETE/CREATE/\\i/扩展命令等）一律保留
    if not re.match(r"(?is)^\s*SELECT\b", s):
        return Classification(Decision.PASS_THROUGH, "not a SELECT statement", masked_sql=masked)

    # 3) 已有 JOIN / 多表能力优先放行
    if re.search(r"(?i)\bJOIN\b", s):
        return Classification(Decision.PASS_THROUGH, "existing JOIN path preserved", masked_sql=masked)

    from_match = re.search(r"(?is)\bFROM\b", s)
    if not from_match:
        return Classification(Decision.PASS_THROUGH, "SELECT without FROM is outside this guard", masked_sql=masked)

    projection = s[:from_match.start()].strip()
    rest = s[from_match.end():].strip()
    where_match = re.search(r"(?is)\bWHERE\b", rest)
    from_clause = rest[:where_match.start()].strip() if where_match else rest
    where_clause = rest[where_match.end():].strip() if where_match else ""

    table = _table_from_from_clause(from_clause)
    if table is None:
        return Classification(Decision.PASS_THROUGH, "multi-table or complex FROM clause preserved", masked_sql=masked)

    # 4) 物理表回查 / 内部直接查询
    if table.lower().endswith(("_data", "_index")) or table == _IDENT_PLACEHOLDER:
        return Classification(Decision.PASS_THROUGH, "physical table query preserved", table=table, masked_sql=masked)

    # 5) 普通非 SSE 表与未登记逻辑表
    if table not in tables:
        return Classification(Decision.PASS_THROUGH, "table not registered as Fides", table=table, masked_sql=masked)

    # 到这里才是「已登记 Fides 表的用户单表 SELECT」，开始检查未支持形式
    proj_body = re.sub(r"(?is)^SELECT\b", "", projection, count=1).strip()
    if not re.fullmatch(r"(?:\*|[A-Za-z_][A-Za-z0-9_$]*\.\*)", proj_body):
        return Classification(Decision.REJECT, "projection not supported (SELECT * required)", table=table, masked_sql=masked)

    if not where_clause:
        return Classification(Decision.REJECT, "single-table keyword WHERE condition required", table=table, masked_sql=masked)

    if re.search(r"(?i)\b(?:AND|OR|LIKE|UNION|INTERSECT|EXCEPT|HAVING|GROUP\s+BY|ORDER\s+BY|LIMIT|OFFSET)\b", where_clause):
        return Classification(Decision.REJECT, "compound / unsupported WHERE form", table=table, masked_sql=masked)
    if ";" in where_clause:
        return Classification(Decision.REJECT, "multiple statements not supported", table=table, masked_sql=masked)

    cond = re.fullmatch(
        r"(?is)\s*(?:[A-Za-z_][A-Za-z0-9_$]*\.)?keyword\s*=\s*(__ZYC_STR\d+__)\s*",
        where_clause,
    )
    if not cond:
        return Classification(Decision.REJECT, "non-string or malformed keyword condition", table=table, masked_sql=masked)

    idx = int(_STR_PLACEHOLDER.fullmatch(cond.group(1)).group(1))
    keyword = literals[idx]
    return Classification(
        Decision.ALLOW_SIMPLE,
        "simple single-table keyword equality query",
        table=table,
        keyword=keyword,
        masked_sql=masked,
    )
