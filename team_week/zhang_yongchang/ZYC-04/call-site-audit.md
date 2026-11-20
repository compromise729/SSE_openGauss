# ZYC-04 客户端调用点审计

审计基线：SHY-04 固定提交 `5c8b2b03b482e203ac65a6f989a1b669126f263c`；同时比对当前 `main` 的相关源码，两者在这些文件上未发现差异。契约：SHY-02 v1.0。

审计目标：按 LZM-04 U03「盘点绕过新版事务/标记的调用入口，统一接入并回归」，区分客户端调用方问题与模块/服务端实现问题。

## 审计结果

| 位置 | 当前行为 | 是否绕过新版事务/标记 | 归属 |
|---|---|---|---|
| `src/bin/psql/common.cpp:1494-1507` `SendQuery()` | `v1_state_selected()` 或 SQL 含 `sse_storage` 时先进入 `sse_v1::execute_request()`；`handled` 后直接 `PrintQueryResults()` 并返回，不再进入旧 SSE 分支。 | 否，用户入口已接入。 | 张永昌 |
| `src/common/interfaces/libpq/row_request_v1.cpp:338-360` `execute_request()` | 无 v1 状态且无 `sse_storage` 标记时返回 `unhandled`，用于保留普通 SQL/旧表路径；有 v1 状态时要求已登记表。 | 否，分流条件明确。 | 张永昌/SHY-03 |
| `row_request_v1.cpp:442-485` SELECT 主路径 | `SearchTransaction` 包住索引取出、重插、rid 回查；`COMMIT` 后才 `save_counters`、`complete_operation`，最后才输出 result。 | 否，核心闭环在事务内。 | 张永昌/SHY-03 |
| `src/common/interfaces/libpq/fe-exec.cpp` | 未发现直接调用 `fides_v1_select` / `sse_v1_take` / `sse_v1_reinsert` 的 v1 业务入口。 | 未发现客户端绕过。 | 张永昌 |
| `src/common/interfaces/libpq/Client_Sophos.cpp:70,89,107,128,235,256,280,301` | 均已按 `v1_state_selected()` 分流到 v1 语义；状态读写仍由刘卓明维护。 | 未发现客户端绕过。 | 刘卓明 |
| `PSQLexec()` 及内部 `PQexec()` 调用 | 用于 gsql 元命令/内部检查，不构成用户业务 SQL 入口；未发现对 v1 物理表的业务读写。 | 未发现业务入口绕过。 | 张永昌 |
| `postgres.cpp:2198` → `Server_Sophos.cpp:131` | `fides_v1_select` 进入 `Sophos_server->search(..., true)`，执行 `public.sse_v1_take(...)`。现行客户端查询和无外层事务的对照都使用此服务端语句。 | 现行调用在 `SearchTransaction` 内。 | 服务端/SQL 负责人、客户端负责人 |
| `team_week/liu_zhuoming/LZM-04/preflight/legacy-control.cpp:23-29` | 故意复现旧式「先取出、后重插」无事务流程，用于诊断 2→0 现场。 | 是测试对照工具，不是生产入口。 | 刘卓明；保留为诊断夹具 |

## 结论（终稿，已结合 2026-11-20 真库复跑）

- 当前客户端用户 SQL 入口 `SendQuery()` 已统一接入 `execute_request()`；在已审计的 `common.cpp`、`fe-exec.cpp`、`Client_Sophos.cpp`、`row_request_v1.cpp` 中，未发现新的客户端调用点绕过 `SearchTransaction` / `begin_checked_operation` / `complete_operation`。
- LZM-04 04-11 在当前入口注入重插失败，事务回滚后索引 2→2，重开阻断。04-12 的诊断工具在无外层事务时先取出、再单独重插，索引 2→0。两个用例均在候选上独立复现。[汇总复验](evidence/combined-review.json)
- 同基线真实复跑结果：官方入口 `zyc_live_verify_all.sh` 退出 0，MODULE=18 / BASELINE_LIVE=18 / ZYC=13 全通过；末行篡改、缺行、批量第二行失败、状态异常重开均以非零退出 + 无部分明文 + DB 不变 + 重开 `incomplete_operation` 结束。未发现需要张永昌修改的客户端调用处。
- 历史索引诊断/重建、跨端原子提交、崩溃恢复和安全重试不在本轮放行范围，属于模块协议问题，已在 `problem-closure.md` 如实标注为未关闭项。
- 修订集中在复验工具与证据记录，生产源码沿用 SHY-04 固定版本。
