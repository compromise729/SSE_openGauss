"""ZYC-01 离线回归：未登记词 / 引号内 AND / 中文空格 / 未支持语法 / 重插返回状态。

运行（本目录内）：
    python -m unittest discover -s . -p 'test_*.py' -v

断言的是调用框架的规则模型和错误分支模型，不链接 libpq、不连接数据库。
SHY-03 真实接口尚未交付，本套件不宣称数据库链路已接通；链路验证属于 ZYC-02/03。
"""

from __future__ import annotations

import unittest

from sql_input_guard import Decision, Origin, classify
from reencrypt_result_guard import ExecStatus, FakePGresult, evaluate_reinsert_result

# 契约 v1.0（SHY-02 release b6159cf8）公开样例命名
TABLE = "week_demo"           # 新密文逻辑表
OLD_TABLE = "zyc_demo_20261115"  # 旧 SSE 表（team_prep 实测基线命名）
APPLE = "apple"
BANANA = "banana"


class CallSiteRoutingTests(unittest.TestCase):
    """按契约把请求分成四类：新密文表 / 旧 SSE 表 / 普通 SQL / 内部请求。

    ZYC-01 只建立调用框架：判定哪些输入进入受保护分支，哪些必须原样放行。
    真正的加密/解密调用在 ZYC-02/03 接入，这里不实现也不模拟密码学结果。
    """

    NEW_TABLES = {TABLE, OLD_TABLE}  # 本框架把已登记 Fides 逻辑表都视为受保护路由

    def test_new_cipher_table_simple_query_enters_guard(self):
        r = classify(f"SELECT * FROM {TABLE} WHERE keyword='{APPLE}';", self.NEW_TABLES)
        self.assertEqual(r.decision, Decision.ALLOW_SIMPLE)
        self.assertEqual(r.keyword, APPLE)

    def test_old_sse_table_simple_query_enters_guard(self):
        r = classify(f"SELECT * FROM {OLD_TABLE} WHERE keyword='{APPLE}';", self.NEW_TABLES)
        self.assertEqual(r.decision, Decision.ALLOW_SIMPLE)

    def test_plain_table_passes_through(self):
        r = classify("SELECT * FROM public.orders WHERE id=1;", self.NEW_TABLES)
        self.assertEqual(r.decision, Decision.PASS_THROUGH)

    def test_internal_request_passes_through(self):
        # 内部重插/回查走 PQexec 直接调用，不进入用户输入检查
        r = classify(
            f"SELECT payload FROM {TABLE} WHERE keyword='{APPLE}' AND ind='1';",
            self.NEW_TABLES,
            Origin.INTERNAL,
        )
        self.assertEqual(r.decision, Decision.PASS_THROUGH)

    def test_physical_table_lookup_passes_through(self):
        for t in (f"{TABLE}_data", f"{TABLE}_index"):
            with self.subTest(table=t):
                r = classify(f"SELECT * FROM {t};", self.NEW_TABLES)
                self.assertEqual(r.decision, Decision.PASS_THROUGH)


class UnsupportedSyntaxTests(unittest.TestCase):
    """未支持语法在改变状态/发送请求之前拒绝。"""

    TABLES = {TABLE}

    CASES = [
        ("AND 复合条件", f"SELECT * FROM {TABLE} WHERE keyword='{APPLE}' AND ind='1';"),
        ("OR 复合条件", f"SELECT * FROM {TABLE} WHERE keyword='{APPLE}' OR ind='2';"),
        ("LIKE", f"SELECT * FROM {TABLE} WHERE keyword LIKE 'app%';"),
        ("投影", f"SELECT payload FROM {TABLE} WHERE keyword='{APPLE}';"),
        ("非字符串关键词", f"SELECT * FROM {TABLE} WHERE keyword=123;"),
        ("参数化", f"SELECT * FROM {TABLE} WHERE keyword=$1;"),
        ("UNION", f"SELECT * FROM {TABLE} WHERE keyword='{APPLE}' UNION SELECT * FROM {TABLE};"),
        ("ORDER BY", f"SELECT * FROM {TABLE} WHERE keyword='{APPLE}' ORDER BY ind;"),
        ("LIMIT", f"SELECT * FROM {TABLE} WHERE keyword='{APPLE}' LIMIT 1;"),
        ("缺 WHERE", f"SELECT * FROM {TABLE};"),
        ("错误的列", f"SELECT * FROM {TABLE} WHERE ind='1';"),
        ("多语句", f"SELECT * FROM {TABLE} WHERE keyword='{APPLE}'; DROP TABLE {TABLE};"),
    ]

    def test_all_unsupported_forms_rejected(self):
        for name, sql in self.CASES:
            with self.subTest(case=name):
                r = classify(sql, self.TABLES)
                self.assertEqual(r.decision, Decision.REJECT, f"{name}: {r.reason}")


class QuotedAndChineseSpaceTests(unittest.TestCase):
    """引号内 AND 不是操作符；中文/空格按完整字符串保留。"""

    TABLES = {TABLE}

    def test_and_inside_quote_is_literal(self):
        r = classify(f"SELECT * FROM {TABLE} WHERE keyword='red AND green';", self.TABLES)
        self.assertEqual(r.decision, Decision.ALLOW_SIMPLE)
        self.assertEqual(r.keyword, "red AND green")

    def test_or_inside_quote_is_literal(self):
        r = classify(f"SELECT * FROM {TABLE} WHERE keyword='it''s OR not';", self.TABLES)
        self.assertEqual(r.decision, Decision.ALLOW_SIMPLE)
        self.assertEqual(r.keyword, "it's OR not")

    def test_chinese_and_space_keyword(self):
        for kw in ("中文 关键词", "红 苹果", "  leading and trailing  "):
            with self.subTest(keyword=kw):
                r = classify(f"SELECT * FROM {TABLE} WHERE keyword='{kw}';", self.TABLES)
                self.assertEqual(r.decision, Decision.ALLOW_SIMPLE)
                self.assertEqual(r.keyword, kw)

    def test_banana_keyword(self):
        r = classify(f"SELECT * FROM {TABLE} WHERE keyword='{BANANA}';", self.TABLES)
        self.assertEqual(r.decision, Decision.ALLOW_SIMPLE)
        self.assertEqual(r.keyword, BANANA)


class EmptyTokenClassificationTests(unittest.TestCase):
    """ZYC-01 调用框架：区分「健康未登记词→本地空集」与「状态/令牌异常→失败」。

    这是规则模型；真实 preflight 状态来自 LZM-02（SHY-03 之后交付）。
    不得仅凭 token 为空就判定没有数据。
    """

    def classify_empty_token(self, state_healthy: bool, keyword_registered: bool):
        """返回 ('empty_result'|'fail', reason)。框架级判定，不产出数据库行。"""
        if not state_healthy:
            return "fail", "state_invalid: broken state requires explicit failure"
        if not keyword_registered:
            return "empty_result", "healthy state + unregistered keyword -> local zero rows"
        # 已登记词却生成空 token 属于协议异常
        return "fail", "protocol_invalid: registered keyword produced empty token"

    def test_healthy_unregistered_keyword_returns_empty_result(self):
        outcome, reason = self.classify_empty_token(True, False)
        self.assertEqual(outcome, "empty_result")
        self.assertIn("unregistered", reason)

    def test_broken_state_fails(self):
        outcome, reason = self.classify_empty_token(False, False)
        self.assertEqual(outcome, "fail")
        self.assertIn("state_invalid", reason)

    def test_registered_keyword_with_empty_token_fails(self):
        outcome, reason = self.classify_empty_token(True, True)
        self.assertEqual(outcome, "fail")
        self.assertIn("protocol_invalid", reason)


class ReinsertGuardTests(unittest.TestCase):
    """重插失败不得打印假成功业务行。"""

    def test_null_result_stops_data_query(self):
        o = evaluate_reinsert_result(None, "server closed connection")
        self.assertFalse(o.ok)
        self.assertFalse(o.continue_data_query)
        self.assertIn("no result returned", o.message)

    def test_fatal_status_stops_data_query(self):
        o = evaluate_reinsert_result(FakePGresult(ExecStatus.PGRES_FATAL_ERROR, "function does not exist", "42883"))
        self.assertFalse(o.ok)
        self.assertFalse(o.continue_data_query)

    def test_success_continues_data_query(self):
        o = evaluate_reinsert_result(FakePGresult(ExecStatus.PGRES_COMMAND_OK))
        self.assertTrue(o.ok)
        self.assertTrue(o.continue_data_query)


if __name__ == "__main__":
    unittest.main(verbosity=2)
