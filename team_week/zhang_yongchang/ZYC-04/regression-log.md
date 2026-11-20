# ZYC-04 针对性回归日志（2026-11-20 真库实测）

## 运行环境与基线

- 主机：CentOS 7 独立实例 `192.168.164.128`，隔离实例端口 55443，omm 私有 mount namespace（LZM-01 isolated.sh）。
- 源码：SHY-04 固定提交 `5c8b2b03b482e203ac65a6f989a1b669126f263c`；VM 工作副本 6 个关键源码文件 SHA-256 与该提交逐字节一致（见 `evidence/zyc13-run/summary.json` 与 `baseline.log`）。
- 产物：gsql `4999e793e49dc21b03bfb47bf53eefa0369e0edd1cc1bab759a1fbd406dec7a9`、gaussdb `7bbdd1e3bc878312af3a8fec05a17bbee9e91ba2826bd5dc1ddc947f82548658`、libpq_ce.so.5.5 `c7da6c48fea2397a732df08e45cd12dd9ac2679b2c4e74e21d037abc078a413f`、SSE_v1.sql `5c1f413d710cf746dd22d5bbc1ad07370316775e945034a158627ded7c44bf34`。
- 入口：组长指定 `team_week/zhang_yongchang/tools/zyc_live_verify_all.sh`（校验文件指纹，不依赖 git 历史）。
- 入口总结果：`ZYC_ENTRY_RC=0`、`EVIDENCE_VALIDATED=PASS`、`ZYC_RESULT=PASS MODULE_CASES=18 BASELINE_LIVE_CASES=18 ZYC_CASES=13`、`ISOLATED_SERVER_STOPPED`。
- 原始证据：`evidence/zyc13-run/`，VM 源目录 `/var/tmp/zyc-acceptance.x3eqf4KY`。

## 一、失败用例针对性回归（缺口 13 与三份报告阻塞项）

下表每行的 stdout 均只有两行初始化信息（`Initializing crypto library` / `cryptoInitWrapper ok`），**无任何业务明文行**；数据库快照在故障前后一致；故障后重开被 `incomplete_operation` 阻断。

| 场景 | 注入方式 | 预期 | 实际（请求号 / stderr 错误码） | 结构化断言 | 结论 |
|---|---|---|---|---|---|
| 批量 INSERT 第二行失败（ZYC-09 / LZM04-04-11 同类） | 触发器使第二行业务行 INSERT 抛异常 | 整批回滚、非零退出、无部分明文、重开阻断 | req25 `Fides db_failed: review injected second-row failure`；req26 `Fides incomplete_operation` | `second_row_failure_rolls_back_entire_batch=true`、`write_nonzero=true`、`database_unchanged=true`、`marker_retained=true`、`reopen_blocked=true` | PASS |
| 业务回查最后一行标签篡改（ZYC-10 / YZH02-R01） | 改写 `sse_v1_fetch`，仅改最后返回行认证标签 | 整次失败、非零退出、无部分明文、DB 不变、重开阻断 | req27 `Fides auth_failed: AES-GCM tag verification failed`；req28 `incomplete_operation` | `last_returned_tag_tampered=true`、`no_partial_plaintext=true`、`database_unchanged=true`、`reopen_blocked=true` | PASS |
| 定向回查缺一条业务行（ZYC-11 / LMD03-回查缺行） | 物理删除已追加行的 `data_blob` | 报不一致、非零退出、无部分明文、重开阻断 | req29 `Fides inconsistent_result: requested 3 rid(s), 2 matched exactly once`；req30 `incomplete_operation` | `physical_business_row_deleted=true`、`no_partial_plaintext=true`、`reopen_blocked=true` | PASS |
| 设施登记失败（ZYC-00） | 破坏初始化登记条件 | 非零退出、无假成功 | 结构化结果 PASS | `registration failure returned nonzero`，重开阻断 | PASS |
| 不支持/超限输入副作用前拒绝（ZYC-08） | AND 复合、列投影、超长载荷 | 副作用前拒绝、状态/DB 不变 | 结构化结果 PASS | `reject-before-effects`、`state_and_database_unchanged=true` | PASS |

## 二、完整正常闭环回归

| 场景 | 预期 | 实际 | 结论 |
|---|---|---|---|
| 新表 CREATE 与登记、三行公开数据写入（ZYC-01/02） | 成功，物理表仅密文 | PASS；物理表 `week_demo_data`/`week_demo_index` 无机读业务明文 | PASS |
| apple 完整行两条、banana 一条、未知词零条（ZYC-03/04） | 完整行多重集合一致 | PASS | PASS |
| 重复搜索 + 追加新行（ZYC-05） | 1/2 → 1/2/4，非整数 rid 正常 | PASS | PASS |
| 中文/首尾空格/空字段/SQL 双单引号转义（ZYC-06） | 逐字符一致 | PASS | PASS |
| 协议内最大载荷往返（ZYC-07） | 完整恢复 | PASS | PASS |
| 令牌/位置/重插/回查/展示阶段记录（ZYC-12） | 10 次成功搜索逐请求阶段追踪，无明文关键词谓词，rid 集合一致 | PASS；`requests/phases.json`、`request-*.wire.jsonl` | PASS |

## 三、覆盖关系对照（本轮 vs 三份报告）

[候选汇总复验](evidence/combined-review.json) 记录每套入口的退出码和版本。张永昌原始 ZYC 用例与组长在候选上复跑的三套报告按以下用例对照。

- **YZH-02 18/18**。原生矩阵覆盖随机化、文本、封装认证与密钥故障。ZYC-10 另验末行认证错误时禁止输出部分明文。
- **LMD-03 11/11**。DB-04 对应 ZYC-09，DB-07 对应 ZYC-11。DB-03 重复 rid、DB-05 索引冲突、DB-06 重插失败由各自原生用例验证。
- **LZM-04 36/36**。04-03 实测 COMMIT 后计数保存失败及重开阻断，04-04 实测 SIGKILL，04-10 实测部分登记后阻断。04-11 当前事务回滚保留两行索引，04-12 无外层事务对照为两行降至零。

## 四、结论

- 缺口 13「认证错误可能只显示单元格错误、查询仍 exit=0」：本轮实测确认任一行失败整次查询失败、非零退出、无部分明文、错误停止模式生效，**已关闭**。
- 三份报告中阻塞正常闭环/错误阻断的核心失败场景：在同基线真实数据库上复现并通过，**客户端接入层无需修改**。
- 未关闭项：跨端原子提交、计数保存失败后的自动恢复、建表部分登记补偿、多库多表隔离、密钥恢复等，属模块协议问题，见 `problem-closure.md` 与 LZM-04 handoff 第八节。
