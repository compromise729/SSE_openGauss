# ZYC-04 证据目录

## 本轮实测（2026-11-20）

- `zyc13-run/`：SHY-04 官方入口 `zyc_live_verify_all.sh` 的原始证据（95 个文件），从 VM 隔离目录 `/var/tmp/zyc-acceptance.x3eqf4KY` 整包拉回。
  - `summary.json`：入口总结果、测试产物 SHA-256 指纹、用例计数。`result=PASS`、`module_cases=18`、`baseline_live_cases=18`、`zyc_cases=13`、`entry_regressions=5`、`successful_search_traces=10`。
  - `baseline.log`：LZM-03 模块 18/18 + 基线真库 18/18 通过，`PASS original client/server state bytes unchanged`、`ISOLATED_SERVER_STOPPED`。
  - `entry-regressions.log`：入口退出码传播 5/5 ok。
  - `requests/`：request-01..13 的完整 `.wire.jsonl`（libpq 调用边界）、`.stdout`、`.stderr`；failure 阶段见 request-25..30；另有 `results.json`（13 项结构化断言）、`phases.json`（10 次成功搜索阶段追踪）、`independent-decode.json`（独立解封业务行/位置）。
- `zyc13-run/requests/request-{25,27,29}.stderr`：三类失败注入的真实错误码 `db_failed` / `auth_failed` / `inconsistent_result`；对应 stdout 只有初始化两行、无业务明文。
- `zyc13-run/requests/request-{26,28,30}.stderr`：故障后重开全部 `incomplete_operation`，证明状态标记与阻断生效。

## 复跑脚本与说明

- `../reproduce.sh`：四入口汇总复跑脚本（ZYC 13 项 + YZH-02 18 项 + LMD-03 11 项 + LZM-04 36 项）；本轮因 VM 精简 git 对象库缺 `5c8b2b03` 祖先，三份报告原生入口未原样重放，改用组长指定的官方 ZYC 入口在同基线覆盖核心阻塞项。
- `../regression-log.md`：针对性回归日志（失败用例退出码、错误码、无部分明文、结构化断言与覆盖关系对照）。

## 说明

- 所有业务明文均来自 SHY-04 公开夹具；派生密钥值在证据中已遮盖（`<redacted>`）。
- 未执行 `reproduce.sh` 中三份报告原生入口的部分如实标注为「未在本 VM 重放」，并引用上游 main 上已发布、经复核的证据，不写成已实测。
