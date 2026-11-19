# SHY-04｜复核真实闭环并发布联合测试基线

组长于 2026-11-19 在独立 CentOS 7 环境完成构建、安装和复验。真实用户 SQL 的插入、检索、客户端认证与解密通过，开放 YZH-02、LMD-03、LZM-04 使用同一版本开展异常联测，ZYC-04 同步接收。

**固定源码提交** `5c8b2b03b482e203ac65a6f989a1b669126f263c`，为 ZYC-02/03 合入 [PR #14](https://github.com/compromise729/SSE_openGauss/pull/14) 后的 `main` 提交。复跑入口为该提交内的 `zyc_live_verify_all.sh`。[公开夹具与预期](公开夹具与预期.md) · [验证摘要](evidence/summary.json)

## 独立复验结果

| 检查 | 实测 | 证据 |
| --- | --- | --- |
| 原生构建及安装 | CentOS 7.9、GCC 7.3.0。复用独立副本的配置和构建缓存，重新编译本轮客户端路径，`make` 与 `make install` 均退出 0。`gsql` 实际链接同批 `libpq_ce.so.5`，服务端使用同批 `gaussdb`。 | [构建结果](evidence/build-result.txt) · [运行环境](evidence/runtime.txt) · [SHA-256](evidence/built-artifacts.sha256) |
| 验收入口回归 | 5/5，覆盖安装缺失、SQL 失败、缺少成功查询追踪和验证失败的退出传播。 | [入口日志](evidence/entry-regressions.log) |
| LZM 状态与密码模块 | 18/18，覆盖密钥读取、流程失败和状态标记次序。 | [模块结果](evidence/lzm-module-results.json) |
| LZM 真实数据库 | 18/18，包含正常搜索、客户端重开、未知词、缺钥、损坏和重插失败。 | [LZM 实库结果](evidence/lzm-live-results.json) |
| ZYC 真实数据库 | 13/13，包含密文物理表、完整行多重集合、文本与载荷边界和失败传播。10 次成功搜索均有阶段记录。 | [ZYC 结果](evidence/results.json) · [阶段记录](evidence/requests/phases.json) |

合计 54 项通过，其中 31 项在真实数据库上运行。总入口退出 0，本轮两个隔离实例均已停止。重启后重新校验了构建产物、14 项测试输入指纹和证据，验证器生成的摘要与原摘要逐字节一致。[运行输出](evidence/verification.log) · [证据校验清单](evidence/manifest-sha256.txt)

## 同一次 `apple` 查询的完整链路

以下以同一个 ZYC 隔离实例的请求 05 为例。请求 04 先从 gsql 用户入口插入两条 `apple` 和一条 `banana`。物理检查读取 `public.week_demo_data` 和 `public.week_demo_index` 各三条记录，并逐项独立认证、解密业务行与位置。rid 是随机生成的 32 位十六进制记录标识。[密文写入记录](evidence/requests/request-04.wire.jsonl) · [独立解封结果](evidence/requests/independent-decode.json)

| 阶段 | 请求 05 实测 |
| --- | --- |
| 客户端输入 | `SELECT * FROM week_demo WHERE keyword='apple';` |
| 服务端物理存储 | 业务表持有 `rid, data_blob`，索引表持有 `ut, locator_blob`。请求 04 以参数数组绑定写入三条，物理检查确认封装用途分别为业务行和位置。 |
| 令牌请求与密文位置返回 | `fides_v1_select` 用令牌和 `add_count=2` 查询 `public.week_demo_index`，返回两组 `(ut, locator_blob)`。证据中的派生密钥值已遮盖。 |
| 客户端位置认证 | 两个位置认证解封得到 rid `cca708e63a79eff194cdf3dfa7603ae4` 和 `fa7b9a692a2e0eb1e25e659a0445ee89`。 |
| 索引重插 | 计数推进两次后调用 `sse_v1_reinsert`。独立解封确认两条新索引位置仍指向上述 rid。 |
| 按 rid 回查 | `sse_v1_fetch` 只接收上述两个 rid，并返回这两个 rid 对应的 `data_blob`。 |
| 本地认证、解密与提交 | 业务行全部认证后本地解密，事务提交后输出 `1\|apple\|red apple` 和 `2\|apple\|green apple`。 |

请求 05、07 的 `add_count` 分别为 2、4，返回同一个完整行多重集合。请求 06 搜索 `banana` 返回一条，请求 08 搜索未知词返回零条，未发出业务检索，客户端状态和数据库快照一致。LZM 的 `LIVE-03` 和 `LIVE-04` 另行直接核对计数分别由 2 增至 4、由 4 增至 6。[请求 05 SQL 与返回](evidence/requests/request-05.wire.jsonl) · [05 输出](evidence/requests/request-05.stdout) · [07 输出](evidence/requests/request-07.stdout) · [请求与退出码](evidence/requests/requests.json)

## 已观察到的错误返回

| 情况 | 实测 | 证据 |
| --- | --- | --- |
| 批量写入第二行失败 | 退出 3，整批回滚，标记保留。 | [25 错误](evidence/requests/request-25.stderr) |
| 业务回查最后一行认证失败 | 退出 3，未输出任何业务明文，事务回滚。 | [27 错误](evidence/requests/request-27.stderr) · [27 输出](evidence/requests/request-27.stdout) |
| 定向回查缺一条记录 | 退出 3，报告 `inconsistent_result`，未输出任何业务明文。 | [29 错误](evidence/requests/request-29.stderr) · [29 输出](evidence/requests/request-29.stdout) |
| 上述故障后各自重开 | 退出 1，报告 `incomplete_operation`。 | [26 错误](evidence/requests/request-26.stderr) · [28 错误](evidence/requests/request-28.stderr) · [30 错误](evidence/requests/request-30.stderr) |

## 复跑

在独立工作副本检出固定提交。在 CentOS 7 中按 [SHY-03 构建环境](../SHY-03/README.md) 和 [LZM-03 原生构建步骤](../../liu_zhuoming/LZM-03/env.md) 配置并安装，确保 `dest` 中是本次构建的一组程序和库。有绝对路径的旧构建配置时，使配置中的路径指向该独立副本。构建使用 `omm`。

```bash
# 在独立 Git 副本中运行
git fetch origin
git checkout --detach 5c8b2b03b482e203ac65a6f989a1b669126f263c
```

原生安装完成后，在外层 Linux root 终端进入该源码目录。缺省端口为 55443，冲突时可用 `--port` 指定空闲端口。入口建立和停止隔离实例，并打印新证据目录。成功退出码为 0。

```bash
repo=$(pwd -P)
bash "$repo/team_week/zhang_yongchang/tools/zyc_live_verify_all.sh" --project-root "$repo"
```

```text
EVIDENCE_VALIDATED=PASS
ZYC_RESULT=PASS MODULE_CASES=18 BASELINE_LIVE_CASES=18 ZYC_CASES=13
```

复验发布附件时，在本任务的 `evidence/` 目录执行 `sha256sum -c manifest-sha256.txt`。独立复建的指纹记入当次报告。附件中的业务明文来自公开夹具，阶段记录来自 libpq 调用边界。

## 异常联测交接

| 负责人和任务 | 待补的范围 |
| --- | --- |
| 严子涵 · YZH-02 | 真实客户端的同明文随机化、错钥与缺钥、位置和业务认证、跨记录替换及失败传播矩阵。 |
| 李孟德 · LMD-03 | 重复 rid、业务回查和重插故障矩阵、索引写失败的事务边界及 profile 汇总。 |
| 刘卓明 · LZM-04 | 真正的进程中断、双客户端竞争、状态与标记写入故障，以及保留状态的服务端重启和 setup。 |
| 张永昌 · ZYC-04 | 汇总三份异常报告，复现阻塞项，验证修复和正常闭环后交接 SHY-05。 |

本次放行范围为基线及已列出的用例。跨端自动恢复、同表合法位置替换或重放与遗漏、多实例同表名隔离仍在本轮验证覆盖之外。各任务按统一的 [夹具、预期及报告字段](公开夹具与预期.md) 回填实际结果。
