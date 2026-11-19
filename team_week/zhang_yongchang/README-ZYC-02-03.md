# ZYC-02 / ZYC-03 集成复验交付说明

本 PR 对应周任务 **ZYC-02｜用户 SQL 到密文写入** 与 **ZYC-03｜索引检索到本地明文展示**。

## 基线结论

- 分支从 `main` 的 `c631fbc2be74…` 切出；该提交已包含 SHY-03 正式发布 `8111044852770529bfacde4de5af5690dd6a8322`（PR #12）。
- 逐文件核对确认：ZYC-02 的三字段 INSERT 解析、rid 生成、行/位置分别加密、`sse_v1_insert`、状态计数；ZYC-03 的令牌检索、`decrypt_locator`、UT 校验/rid 去重、`sse_v1_reinsert`、`sse_v1_fetch`、`decrypt_row`、一致性检查及最终 `PrintQueryResults`，均已在 `row_request_v1.cpp` 与 `common.cpp` 中由 SHY-03/LZM-03 落地。
- 因此本 PR **不重写已发布的 C++ 源码**，只提交两份任务的验收材料与可复跑工具，避免与集成版产生冲突分支。

## 目录

- `ZYC-02/`：handoff.md、insert-report.md、SQL 夹具、构建证据、manifest。
- `ZYC-03/`：handoff.md、search-report.md、SQL 夹具、完整真实会话日志、manifest。
- `tools/zyc_live_verify_all.sh`：在 LZM-01 隔离会话中运行的 ZYC-02/03 合并验收脚本。

## 实测摘要

- 原生构建：`MAKE_EXIT=0`、`INSTALL_EXIT=0`。
- 真实数据库：CREATE 与三条 INSERT 均 rc=0；物理表仅密文。
- 检索：apple 2 行、banana 1 行、unknown 0 行；重复 apple 查询不丢不重。
- 中文/空格关键词与 SQL 双单引号转义通过。
- AND 复合查询与列投影被拒绝（rc=3）。
- data/index 表未发现业务明文或原始 rid 泄露。
- 隔离实例正常停机。

## 复现

1. 按 `ZYC-02/evidence/build-evidence.txt` 的环境在 CentOS 7 / GCC 7.3 构建：`make -j6 && make install prefix="$PWD/dest"`。
2. 用 `team_week/liu_zhuoming/LZM-01/isolated.sh init` 启动隔离实例。
3. 在隔离会话中执行 `tools/zyc_live_verify_all.sh`。
4. 完整结果见 `ZYC-03/evidence/live-full.log`。

## 飞书交接

- ZYC-02：https://nankai.feishu.cn/drive/folder/Rb8vfMFh1lXiG3dlH9Cc46U7nFe
- ZYC-03：https://nankai.feishu.cn/drive/folder/GyOVfZvszlW1P6dgtdCci4Xwnoa
