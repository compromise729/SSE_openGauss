# ZYC-04 交付：接收联合问题并完成客户端修复

本目录对应周任务 **ZYC-04｜接收联合问题并完成客户端修复**（前置 YZH-02 / LMD-03 / LZM-04，基线 SHY-04）。交给组长 SHY-05。

## 基线结论

- SHY-04 固定提交 `5c8b2b03b482e203ac65a6f989a1b669126f263c`；本地工作副本 6 个关键源码文件 SHA-256 与该提交逐字节一致。
- 逐文件审计 `common.cpp` / `row_request_v1.cpp` / `fe-exec.cpp` / `Client_Sophos.cpp`：当前用户 SQL 入口已统一经 `execute_request()` → `SearchTransaction` / `begin_checked_operation` / `complete_operation`，**未发现需要张永昌修改的客户端调用处**。
- 因此本 PR 不提交任何源码改动，也不以跳过认证、取消错误检查或清状态的方式让用例变绿；只交付审计报告、问题关闭表、复跑脚本和真库证据。

## 张永昌原始真库实测（2026-11-20）

- 入口：`team_week/zhang_yongchang/tools/zyc_live_verify_all.sh`（校验当前 Git 检出和安装产物指纹）。
- 结果：`ZYC_ENTRY_RC=0`、`ZYC_RESULT=PASS MODULE_CASES=18 BASELINE_LIVE_CASES=18 ZYC_CASES=13`、`EVIDENCE_VALIDATED=PASS`、隔离实例正常停机。
- 缺口 13 与三份报告阻塞项由失败注入用例实测覆盖：
  - 批量第二行写入失败 → `db_failed`、整批回滚、重开 `incomplete_operation`（request-25/26）；
  - 末行标签篡改 → `auth_failed`、无部分明文、DB 不变、重开阻断（request-27/28）；
  - 定向回查缺行 → `inconsistent_result`、无业务明文、重开阻断（request-29/30）。
  - 三类故障的 stdout 只有初始化两行，无任何业务明文行。

## 交付目录

| 文件 | 作用 |
|---|---|
| `call-site-audit.md` | 客户端调用点审计（终稿），区分客户端/服务端旧路径 |
| `problem-closure.md` | 问题关闭表（终稿），已关闭/未关闭逐条列明 |
| `regression-log.md` | 针对性回归日志，失败用例错误码与结构化断言 |
| `handoff.md` | 交接记录（终稿），含接手人 SHY-05 与未关闭项 |
| `reproduce.sh` | 四入口汇总复跑脚本 |
| `manifest-sha256.txt` | 交付文件 SHA-256，在本目录运行 `sha256sum --strict -c manifest-sha256.txt` |
| `evidence/zyc13-run/` | 官方入口真库原始证据（95 文件：阶段 wire、stdout/stderr、结构化结果、独立解封） |

## 复现

合入顺序为 YZH-02 → ZYC-04。独立检出需包含 YZH-02、LMD-03、LZM-04 工具与 SHY-04 固定源码，`omm` 需能读取检出和 Git 索引。按 [SHY-04 构建说明](../../leader/SHY-04/README.md) 准备同批安装产物，并保留 [LZM-04 构建说明](../../liu_zhuoming/LZM-04/env.md) 所需的配置和对象缓存。旧构建使用固定绝对路径时，按该说明在私有挂载命名空间中映射独立副本。

在外层 CentOS 7 root 终端进入该副本，执行

```bash
bash team_week/zhang_yongchang/ZYC-04/reproduce.sh --repo "$(pwd -P)"
```

入口创建新证据目录并输出路径，`summary.log` 记录检出提交和四套结果。验收要求各套退出码为 `0`，均报告通过与停机，汇总输出 `ZYC04_REPRODUCE_EXIT=0`。样例结果见 [汇总复验记录](evidence/combined-review.json)。

## 范围与未关闭项

- 原始 VM 使用官方 ZYC 入口。组长在含 YZH-02 工具的候选上运行了四套原生入口，实际版本和结果见 [汇总复验记录](evidence/combined-review.json)。
- 未关闭项（跨端原子提交、计数保存失败后自动恢复、建表部分登记补偿、多库多表隔离、密钥恢复等）属模块协议问题，见 `problem-closure.md`，交接相应负责人，不在本轮宣称已解决。

## 飞书交接

- ZYC-04：https://nankai.feishu.cn/drive/folder/UztnfvHvbl3GK5dZGOKcjXM3nrf
