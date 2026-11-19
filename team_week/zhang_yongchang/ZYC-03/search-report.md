# ZYC-03｜索引检索到本地明文展示 报告（search-report.md）

> 任务：ZYC-03（截止 2026-11-19 18:00，前置 ZYC-02）。负责人：张永昌。
> 结论：**在 ZYC-02 同一集成提交与同一实测会话上完成检索链路验收，放行条件全部通过。**

## 1. 输入与版本

|项|内容|
|---|---|
|前置|ZYC-02（本目录同批验收 PASS），源提交 `c631fbc2`（SHY-03 main，含 `8111044`）|
|契约|SHY-02 v1.0；SHY-03 搜索事务约定（同一 DB 事务内取出索引→校验 UT→认证位置→重插→按 rid 回查→提交→清标记→展示）|
|本次 RUN_DIR|`/var/tmp/fides-isolated.rAJ5U9Lm`（写入与检索同一会话；实例正常停机）|
|原始日志|`evidence/live-full.log`（210 行，含每步 stdout/stderr）|

## 2. 检索链路实际核对（源码级）

`row_request_v1.cpp` 的 SELECT 分支（`execute_request()` 尾部）逐项覆盖任务书 8 项要求：

|任务书要求|实现落点|
|---|---|
|本地关键词生成令牌|`client->search_request(keyword)`；校验 `token/derivation_key` 非空、`add_count` 与持锁计数一致|
|服务端索引返回 + 位置认证解密|`fides_v1_select … token/derivation_key/add_count` 返回 `ut,locator_blob`；校验 UT 属于 `expected_tokens(request)` 且不重复；`decrypt_locator(scope,blob,keys,decoded)`，操作必须 0|
|保留 Fides 重插 + 检查返回状态|`SearchTransaction` 包住取出/重插/回查；重插 `sse_v1_reinsert` 经 `query()` 检查 PGresult 状态，失败 `db_failed` 并回滚|
|按 rid 回查 + 集合核对|`sse_v1_fetch` 返回 rid/data_blob；`unique_rids.erase(rid)!=1` 或残留 → `inconsistent_result`|
|全部认证成功后才打印|所有行 `decrypt_row` + keyword 核对完成 → 构造 `row_result` → `complete_operation` → `output`，`common.cpp:1504` 才 `PrintQueryResults`|
|未知词 / 坏状态 / 空令牌分类|`preflight` 未登记词 → 直接 `row_result(conn,{})` 空集（`unknown_empty`）；已登记但空 token → `protocol_invalid`；状态损坏 → `state_invalid`|
|Enc(rid,op) 解密接新过滤器|位置封装认证解密得原始 16 字节 rid + op；按**完整 32 hex 字符串 rid** 去重，不经旧 `stoi`；`Client_Sophos.cpp` 状态读写由刘卓明负责，本轮未改其区域|
|关键词只在客户端生成令牌|SQL 只携带 token/derivation_key/add_count，无明文 keyword 谓词；命中经真实 Fides 索引，不下载整表筛选|

## 3. 真实检索验收结果

|用例|输入|预期|实际|结论|
|---|---|---|---|---|
|apple|`SELECT * FROM week_demo WHERE keyword='apple'`|完整行 2 条|编号 1/2 各一次：red apple、green apple，rc=0|PASS|
|banana|`… keyword='banana'`|1 条|编号 3：yellow banana，rc=0|PASS|
|未知词|`… keyword='zyc_unknown_20261119'` / `not_registered_zyc`|0 行|`(0 rows)`，rc=0（正常空集，非错误）|PASS|
|重复查询|再查 apple|不丢不重|仍 2 行（1、2 各一次），rc=0|PASS|
|中文+空格|`… keyword='中文 苹果 space'`|2 行|编号 4/5：red 苹果、green 苹果，rc=0|PASS|
|SQL 转义|`… keyword='it''s OR not'`|1 行|`it's OR not` / `o'brien apple`，rc=0|PASS|
|未支持语法 AND|`WHERE keyword='apple' AND payload='red apple'`|发送前拒绝|rc=3（非零），无明文输出|PASS|
|未支持投影|`SELECT ind FROM week_demo WHERE keyword='apple'`|发送前拒绝|rc=3（非零）|PASS|
|服务端明文泄露|扫描 data/index 全表 blob|无明文|`NO_PLAINTEXT_IN_DATA_OK` / `NO_PLAINTEXT_IN_INDEX_OK`|PASS|
|批处理退出码|ON_ERROR_STOP=1 下错误语句|非零传播|AND/投影 rc=3|PASS|

放行条件对照：apple 完整行两条 ✅、banana 一条 ✅、未知词零条 ✅、重复查询不丢不重 ✅、无业务明文进入服务器 ✅。

## 4. 分阶段证据（同一会话）

`evidence/live-full.log` 中各阶段可定位：
- 令牌请求/索引取出：`SELECT apple` 段（token/derivation_key 由客户端生成，日志不记密钥）
- 加密位置返回：STEP6 `week_demo_index` 的 `ut,locator_blob` 三行密文
- 认证后 rid 集合与回查：`decrypt_locator`/`decrypt_row` 在返回明文段前完成（源码核对 + 结果一致性）
- 最终明文：STEP7/8/9/10 与 ROUND2 各 SELECT 的输出
- 重插：位于 `execute_request` SELECT 分支的 `sse_v1_reinsert`（成功路径 rc=0，索引维持 3 行）

## 5. 未完成项 / 边界（不得宣称已解决）

- **重插失败后的索引缺口**：检出/阻断/保留标记已在 SHY-03 实现（其 LIVE-15 验证），但自动恢复、跨端原子提交、崩溃恢复、安全重试**未实现**。本轮正常闭环未注入该故障，相关结论引用 SHY-03 证据，不记为本人新增实测。
- 最后一行标签篡改、缺业务行等 fault-injection（YZH-02/LMD-03/ZYC-04 范围）本轮未逐一注入；不在 ZYC-03 放行清单。
- 服务端重启后状态延续、第二客户端竞争、持久化写故障注入属后续任务；本轮未验证。
- Client_Sophos.cpp 状态读写与过滤逻辑分属刘卓明，本轮未改其文件；新版字符串 rid 过滤在 `row_request_v1.cpp` 内完成。

## 6. 复现命令（摘要）

```bash
# 承接 ZYC-02 的隔离实例与 v1 状态
state="$ISOLATED_RUN_DIR/evidence/v1-state"
vsql(){ SSE_V1_STATE_DIR="$state" "$B/dest/bin/gsql" -X \
  -h "$ISOLATED_RUN_DIR/socket" -p 55439 -d postgres -U omm -v ON_ERROR_STOP=1 "$@"; }
vsql -c "SELECT * FROM week_demo WHERE keyword='apple';"    # 2 行
vsql -c "SELECT * FROM week_demo WHERE keyword='banana';"   # 1 行
vsql -c "SELECT * FROM week_demo WHERE keyword='unknown';"  # 0 行
vsql -c "SELECT * FROM week_demo WHERE keyword='apple';"    # 仍 2 行
```
