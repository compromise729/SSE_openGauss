# ZYC-04 交接记录：接收联合问题并完成客户端修复

> 状态：终稿。2026-11-20 已在 CentOS 7 独立实例完成同基线真库复跑，官方入口退出 0，13/13 ZYC + 18 模块 + 18 基线用例全部通过。

|项|内容|
|---|---|
|前置任务编号|YZH-02、LMD-03、LZM-04；基线 SHY-04|
|实际输入提交号|SHY-04 固定提交 `5c8b2b03b482e203ac65a6f989a1b669126f263c`；审计时 main `0d9584ea0933`（含 PR #14/#15/#16）。VM 工作副本 6 个关键源码文件 SHA-256 与该固定提交逐字节一致|
|合同版本|SHY-02 v1.0，`sse_v1_contract.hpp`，`kProtocolVersion=1`|
|本人输出提交号/补丁|无客户端源码补丁：源码审计 + 真库复跑未发现需张永昌修改的调用处。交付审计报告、问题关闭表（终稿）、复跑脚本和真库证据，不伪造源码修复、不以绕过方式使用例通过|
|改动文件及原因|`call-site-audit.md`（调用点审计，终稿）、`problem-closure.md`（问题关闭表，终稿）、`reproduce.sh`（四入口复跑脚本）、`evidence/zyc13-run/`（本轮真库原始证据 95 文件）。原因：满足任务书「复现/区分/关闭清单/回归日志」交付|
|构建/运行命令|组长指定入口：`sudo bash team_week/zhang_yongchang/tools/zyc_live_verify_all.sh --project-root <repo> --port 55443`；四入口汇总见 `reproduce.sh`|
|测试输入与预期/实际|SHY-04 公开夹具 + ZYC 13 项。预期 ZYC=PASS 13；实际 `ZYC_RESULT=PASS MODULE_CASES=18 BASELINE_LIVE_CASES=18 ZYC_CASES=13`，`ZYC_ENTRY_RC=0`，`EVIDENCE_VALIDATED=PASS`|
|原始日志路径|`evidence/zyc13-run/`（summary.json、baseline.log、entry-regressions.log、requests/request-01..13 的 .stdout/.stderr/.wire.jsonl、phases.json、independent-decode.json）；VM 源目录 `/var/tmp/zyc-acceptance.x3eqf4KY`|
|未完成项|三份报告原生脚本因 VM 精简 git 对象库缺 `5c8b2b03` 祖先未原样重放（已用组长指定官方入口在同基线覆盖核心阻塞项并引用上游复核证据）；跨端原子提交/部分登记/多库多表等模块协议问题为未关闭项，见 `problem-closure.md`|
|接手人|组长 SHY-05；YZH-02/LMD-03/LZM-04 负责人复核各自问题|
|复现命令|`sudo bash team_week/zhang_yongchang/tools/zyc_live_verify_all.sh --project-root <独立检出目录> --port 55443`；汇总脚本 `bash team_week/zhang_yongchang/ZYC-04/reproduce.sh --repo <独立检出目录>`|

## 本轮实测摘要（2026-11-20）

- 入口结果：`ZYC_ENTRY_RC=0`、`EVIDENCE_VALIDATED=PASS`、`ZYC_RESULT=PASS MODULE_CASES=18 BASELINE_LIVE_CASES=18 ZYC_CASES=13`、`ISOLATED_SERVER_STOPPED`。
- 13 项逐项 PASS：ZYC-00 登记失败、01 建表、02 三行写入、03 物理密文、04 重开/未知词、05 追加与重复、06 UTF8/引号/空字段、07 最大载荷、08 副作用前拒绝、09 写失败、10 末行认证失败、11 回查缺行、12 wire 阶段。
- 缺口 13（认证错误非零退出、无部分明文、错误停止）由 ZYC-09/10/11 实测覆盖：非零退出、`db_failed`/`auth_failed`/`inconsistent_result`、stdout 无业务行、DB 快照不变、重开被 `incomplete_operation` 阻断。
- 同批产物指纹：gsql `4999e793…`, gaussdb `7bbdd1e3…`, libpq_ce.so.5.5 `c7da6c48…`, SSE_v1.sql `5c1f413d…`（完整值见 `evidence/zyc13-run/summary.json`）。

## 未关闭项（如实交接，不得宣称已解决）

1. 搜索重插后 COMMIT 成功但本地计数保存失败造成跨端不一致：本轮仅验证阻断生效、无假成功、无部分明文；不自动恢复丢失索引，不实现跨端原子提交/崩溃恢复/安全重试。
2. 建表服务端登记与本地版本/映射可能只完成一半：阻断生效，无自动补偿。
3. 同业务编号用新 rid 再插仍可能产生两条业务行；不承诺业务编号唯一/覆盖语义。
4. 多库多表计数命名空间、密钥恢复/轮换、历史故障数据、范围/TLS、原生列加密组合 DDL、Diana/Mitra/Orion 等旧 SSE 方案边界：均不在本轮放行范围。

详见 `problem-closure.md` 关闭清单和 LZM-04 handoff 第八节。

## 签收区（接手人填写）

- 接收状态：待组长填写「已接收」或「退回原因」。
- 复核要点：确认使用同一固定提交指纹；核对 13/13 + 18 + 18 结果；确认未通过跳过认证、取消错误检查或清状态使用例变绿；确认未关闭项如实标注并已交接。
