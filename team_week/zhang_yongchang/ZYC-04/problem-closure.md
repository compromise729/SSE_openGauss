# ZYC-04 问题关闭表（终稿）

状态定义：`已关闭-无需本人改动` / `已关闭-调用处已修复` / `未关闭-模块问题` / `已由上游修复并复核` / `未验证`。

本轮真库复跑于 2026-11-20 在 CentOS 7 独立实例 `192.168.164.128` 完成。VM 工作副本 6 个关键源码文件（common.cpp / row_request_v1.cpp / fe-exec.cpp / Client_Sophos.cpp / Server_Sophos.cpp / SSE_v1.sql）SHA-256 与 SHY-04 固定提交 `5c8b2b03` 逐字节一致，构建产物为同批 gsql/gaussdb/libpq_ce.so.5.5。

## 输入

| 项 | 值 |
|---|---|
| SHY-04 固定基线 | `5c8b2b03b482e203ac65a6f989a1b669126f263c` |
| 审计时 main | `0d9584ea0933`（含 PR #14/#15/#16） |
| 契约 | SHY-02 v1.0，`kProtocolVersion=1` |
| 本轮实测入口 | 组长指定的 `team_week/zhang_yongchang/tools/zyc_live_verify_all.sh`（校验文件指纹，不依赖 git 历史） |
| 本轮实测结果 | `ZYC_RESULT=PASS MODULE_CASES=18 BASELINE_LIVE_CASES=18 ZYC_CASES=13`，`ZYC_ENTRY_RC=0` |

## 关于原生报告脚本的说明（如实记录）

张永昌的原始 VM 缺少基线 Git 对象，使用 ZYC 官方入口覆盖客户端关键场景。组长在完整 Git 检出的候选上复跑了四套原生工具。候选包含 YZH-02 工具，运行源码固定为 SHY-04。用例计数、实际检出和退出码见 [汇总复验记录](evidence/combined-review.json)。

## 关闭清单

| 编号 | 来源与问题 | 分类 | 本轮处理与实测 | 状态 | 证据 |
|---|---|---|---|---|---|
| ZYC-09 | 批量 INSERT 第二行失败必须整批回滚、非零退出、重开阻断 | 客户端闭环 | 真库注入触发器使第二行失败；实测非零退出、`db_failed`、DB 未变、标记保留、重开 `incomplete_operation` | 已关闭-实测通过 | `evidence/zyc13-run/requests/request-*.stderr`；PASS ZYC-09-write-failure |
| ZYC-10 / YZH02-R01 | 多行回查最后一行标签被篡改，不得输出部分明文 | 业务行认证（模块 YZH-01，客户端接入） | 真库改写 `sse_v1_fetch` 末行标签；实测非零退出、`auth_failed`、stdout 无业务行、DB 快照不变、重开阻断 | 已关闭-实测通过 | PASS ZYC-10-last-row-authentication；YZH-02 `last-row-authentication.json`（退出 3） |
| ZYC-11 / LMD03-回查缺行 | 定向回查缺一条业务行，必须报不一致而非展示部分结果 | 结果一致性（客户端接入） | 真库删除一行 `data_blob`；实测非零退出、`inconsistent_result`、无业务明文、DB 回滚 | 已关闭-实测通过 | PASS ZYC-11-missing-fetched-row；LMD-03 review-results.json |
| ZYC-12 | 令牌/位置/重插/回查/展示阶段记录，未下载整表筛选 | 客户端验证 | 10 次成功搜索逐请求阶段追踪；wire 形状、rid 集合一致性、无明文关键词谓词均通过 | 已关闭-实测通过 | PASS ZYC-12-wire-phases；`requests/phases.json`、`request-*.wire.jsonl` |
| ZYC-06 / YZH02-N02 | 中文、首尾空格、空字段、SQL 双单引号转义 | 文本边界 | 真库用例通过 | 已关闭-实测通过 | PASS ZYC-06-utf8-quote-empty |
| ZYC-05 / LMD03-追加 rid | 追加新行后重复查询不丢不重、非整数 rid | 追加与 rid 唯一 | 真库用例通过 | 已关闭-实测通过 | PASS ZYC-05-append-and-repeat |
| ZYC-07 / LMD03-最大载荷 | 协议内最大载荷完整往返，超限在副作用前拒绝 | 载荷边界 | 真库用例通过 | 已关闭-实测通过 | PASS ZYC-07-maximum-payload |
| ZYC-00/08 | 登记失败、不支持输入在副作用前拒绝 | 入参阻断 | 真库用例通过 | 已关闭-实测通过 | PASS ZYC-00-registration-failure / ZYC-08-reject-before-effects |
| YZH-02 原生矩阵 | 随机化、文本边界、业务行与位置认证、跨记录/跨表替换、错钥/缺钥/短钥 | 密码模块与客户端错误传播 | 原生矩阵 18/18。ZYC-10 单独验证末行认证错误时整次查询退出且无业务输出。 | 基线行为已复验 | [YZH-02 故障矩阵](../../yan_zihan/YZH-02/fault-matrix.md)、[汇总复验](evidence/combined-review.json) |
| LMD-03 DB-01～11 | 重复 rid、写入和重插失败、定向回查、封装边界 | 数据库/SQL 一致性 | 原生夹具 11/11。DB-04 对应 ZYC-09 第二行业务写入失败，DB-07 对应 ZYC-11 定向回查缺行。DB-03 重复 rid、DB-05 索引冲突、DB-06 重插失败各有独立实测。 | 基线行为已复验 | [LMD-03](../../li_mengde/LMD-03/README.md)、[汇总复验](evidence/combined-review.json) |
| LZM-04 04-01～36 | 持久标记、计数、真实中断、重启、并发、部分登记和重插 | 状态模块 | 原生夹具 36/36。04-03 注入 COMMIT 后计数保存失败，04-04 实际 SIGKILL，04-10 验证部分登记阻断，04-11/12 验证当前事务和旧式对照。 | 现有阻断行为已复验 | [LZM-04](../../liu_zhuoming/LZM-04/handoff.md)、[汇总复验](evidence/combined-review.json) |
| LZM04-U03 | 重插失败和旧式对照 | 客户端事务边界与服务端取索引 | 两条路径均经 `sse_v1_take`。现行客户端以 `SearchTransaction` 包住取索引、重插和回查，04-11 注入故障后索引 2→2。04-12 无外层事务的对照为 2→0。 | 当前入口的失败回滚已复验，历史索引恢复待处理 | [调用点审计](call-site-audit.md)、[汇总复验](evidence/combined-review.json) |
| LZM04-U01 | COMMIT 后本地计数保存失败造成跨端不一致 | 状态 + SQL 协议 | 04-03 复现数据库先提交、计数保存失败。客户端停止展示，保留标记，新进程报 `incomplete_operation`。 | 未关闭，待协同设计自动恢复 | [LZM-04 U01](../../liu_zhuoming/LZM-04/handoff.md) |
| LZM04-U02 | 建表服务端登记与本地版本/映射可能只完成一半 | 状态 + SQL 协议 | 04-10 复现服务端已登记、本地映射保存失败。保留标记，新进程阻断。 | 未关闭，待补偿流程 | [LZM-04 U02](../../liu_zhuoming/LZM-04/handoff.md) |
| LZM04-U04~U12 | 多库多表计数、密钥恢复、历史故障、范围/TLS、构建路径、系统级故障 | 多模块/范围外 | 不由 ZYC-04 客户端调用处修复解决 | 未关闭-如实标出 | LZM-04 handoff 第八节 |

## 本轮源码修改结论

- 对照任务书「由相应文件负责人修改模块，张永昌修改调用处」：本轮源码审计 + 真实复跑未发现需要张永昌修改的客户端调用处缺陷。所有阻塞正常闭环/错误阻断的失败场景，其认证、终止、回滚、重开阻断行为在当前基线中已正确接入并通过实测。
- 因此**不伪造源码修复提交**，也不通过跳过认证、取消错误检查或清状态使用例变绿。交付物为审计报告、问题关闭表、复跑脚本和真库证据。
- 未关闭项均为跨端原子性/部分登记/多库多表等模块协议问题，归属状态、SQL、密码模块负责人，已如实列入上表并交接 SHY-05。

## 放行条件对照

| 任务放行条件 | 本轮结论 |
|---|---|
| 按三份报告的输入、版本和失败输出复现问题，区分客户端调用方与模块实现问题 | 候选复验使用三套报告的原生入口。逐类验证认证、事务回滚和状态阻断，并记录各自用例。 |
| 由相应文件负责人修改模块，张永昌修改调用处，不得跳过认证/取消检查/清状态 | 完成：未发现需改的本人调用处；没有以任何绕过方式使用例通过 |
| 合入修复提交并重跑每个失败用例及完整正常闭环，形成已关闭/未关闭清单 | 本地候选按 YZH-02 → ZYC-04 准备，四套汇总和未关闭事项已记录。实际主线提交在合入后回填至 SHY-05。 |
| 所有阻塞正常闭环或错误阻断的问题均有修复与实测；未关闭项如实标出 | 完成：阻塞项均有实测通过；跨端一致性/部分登记等未关闭项如实标注并交接 |
