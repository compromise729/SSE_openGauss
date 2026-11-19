# LMD-01 数据库接口交接

2026-11-18 · 李孟德 · 交接给严子涵、张永昌及组长。

## 前置与版本

前置任务为 SHY-02，运行环境来自 SHY-01 和 LZM-01。已核对 SHY-02 v1.0 交付包的 33 个文件。本机使用契约文件包和实际检出的 SHY-01 源码。LZM-01 使用已交付的 `isolated.sh`，源码基准与 SHY-01 一致。

| 版本 | 标识 |
|---|---|
| SHY-01 源码基准 | `25489f563f25b522d0cd37a6c85865039e3f340f` |
| SHY-02 v1.0 发布记录 | `b6159cf8d0dd6d3d8befa8fd84816d289a7b88d1` |
| 已编译验证的服务端源码提交 | `f99e41872fc3f00f9199ed94b5828859e89ed27e` |
| 本地交付分支及提交 | `feature/LMD-01-storage` · `1294a76f37237196b1b208b9187bd7239365ef48` |
| 正式 PR 目标 | `integration/SHY-03` |

上表的本地版本对应原始日志。团队正式 SHY-02 基线是 `7bace5b2182552f1e2fe5865ac66d58812435510`。LMD-01 在 CentOS 复验 `52aa69d36ca6884572e9b9c07c104ad898429e1f` 通过后，经 [PR #6](https://github.com/compromise729/SSE_openGauss/pull/6) 合入 `integration/SHY-03`，合入提交为 `5074c844a306d97002cc4a30ca80d5a929a6898d`。复验覆盖了回查逐 rid 校验与搜索累计计数上限。

## 交付文件与修改原因

| 文件 | 内容与用途 |
|---|---|
| [SSE_v1.sql](windows-original/SSE_v1.sql) | 安装至 `sse_sql/SSE_v1.sql`。实现密文主表、加密索引、版本登记、批量写入、重插及定向回查。 |
| [LMD-01-storage.patch](LMD-01-storage.patch) | Git 分支完整补丁，含上述 SQL、检查和复跑工具。服务端改动位于 `postgres.cpp`、`Server_Sophos.cpp` 和 `Server_Sophos.h`，使新版入口复用 Fides 推导令牌并返回加密位置。 |
| [李孟德-check.sql](李孟德-check.sql) | 增加只读 `fides_v1` profile，检查登记、表结构、主键、封装和函数签名。 |
| [run-module.sh](run-module.sh) 与 [storage-fixture.sql](storage-fixture.sql) | 统一定位并执行建表、写入、回查、回滚、映射及只读检查用例，脚本汇总退出码并输出 JSON 结论。 |
| [db-interface.md](db-interface.md) 与 [module-test.log](module-test.log) | 接入约定、构建、复跑命令和原始执行输出。 |
| [results.json](results.json) | 原始自测的 Git 版本、构建指纹，以及正式复验和合入记录。 |
| [manifest-sha256.json](manifest-sha256.json) | 本目录报告、补丁、脚本、SQL 和日志的文件摘要。 |

源码与可复跑工具由 Git 分支交接。飞书材料包括本报告、接口说明、合成夹具、测试日志、结果和构建指纹。若组长代导入补丁，先校验 SHA-256，再在指定基准应用，随后回填远端分支、PR 和合入提交。

从已应用补丁的源树运行 `make -j8` 并安装新服务端。启动 LZM-01 新隔离实例，加载 `SSE_init.sql` 后运行 `bash "$deliverables/LMD-01/run-module.sh" "$ISOLATED_RUN_DIR/socket" "$ISOLATED_PORT" postgres "$ISOLATED_RUN_DIR/client"`。完整命令和前提见接口说明。本次复跑端口为 55462。实际提交、服务端二进制和入口脚本的 SHA-256 位于日志开头，`version()` 的旧 build 字段来自增量编译环境继承的生成元数据。[数据库接口说明](db-interface.md) · [模块日志](module-test.log)

## 验收结果

| 任务书条目 | 输入 | 预期 | 实测及证据 |
|---|---|---|---|
| 1. 密文表、索引和版本 | `lmd_sample` 新表 | `rid` 与 `ut` 为各自主键，只有两类密文列和 v1 登记 | 新版 profile 验证了签名、列、主键、封装与登记。 |
| 2. 写入、重插、回查 | 合成双行数据、中间重复 rid、索引 ut 冲突 | 双表写入全成或全退，重插只动索引 | 失败前后两表行数及摘要一致，正常重插和定向回查通过。真实数据库异常见 LMD-02。 |
| 3. Fides 路径 | 公开样例，现有 Fides 生成的请求、空请求及已消费令牌 | 原样返回 `ut, locator_blob`，未命中零行 | LMD-02 两次命中 apple 两条，空请求及已消费令牌零行。 |
| 4. 合成字节夹具 | 88 和 65,624 字节业务封装、96 字节位置封装 | 结构和字节不变，超限拒绝 | 两种业务长度和位置封装均按字节匹配，超限失败。该夹具只验证结构与传输。 |
| 5. 检查脚本 | 新旧 profile、缺表、错 profile、重复编号、合成 FAIL 与 SKIP 提示 | 只读，必测失败或跳过返回非零 | 校验前后相同，三类真实检查失败均退出 3。汇总器捕获成功退出的 FAIL 和 SKIP 提示并返回 4。末行包含机器可读 JSON。 |
| 6. 建表失败与登记 | 正常创建、catalog 登记拒绝、旧表调用新版入口 | 成功才登记，失败不残留物理表、catalog 行或测试映射，旧表拒绝 | 模块验收通过。正常建立版本 1，故障退出码 3，双表和登记均回滚，两份测试映射 SHA-256 一致，新版入口拒绝旧表。 |
| 7. 扩张、转义和去辅助列 | 合成上限、真实最大行、Base64 和按参读取 | 参数与返回长度无截断、无旧辅助副本 | 物理行封装 65,624 字节、真实 45,144 字节均原样回读。数组按参数传递，两张表均无 `join_col`。 |

表内 DB-01 和合成夹具原始输出见模块日志，真实 Fides 查询和 DB-02 的真实三行往返见 LMD-02。[模块日志](module-test.log) · [LMD-02 交接](../LMD-02/handoff.md)

## 第 6 项验收范围与下游交接

第 6 项已通过 LMD-01 模块验收。真实数据库负责返回成功或失败、原子登记和阻止旧表进入新版协议。测试调用方验证按该结果决定是否写版本映射。依照团队总览，正式用户 SQL 入口由 ZYC-02 在 SHY-03 后接入，持久化状态由 LZM-02 提供。正式客户端的映射与重开核验由下游接续任务执行。

联测时使用新隔离环境，为 catalog 添加仅拒绝 `lmd_registration_fail` 的检查约束，然后用真实 gsql 执行该表的 ROW_AEAD_V1 建表语句。保存 SQL 错误和批处理非零退出，检查 catalog 与两张物理表均无目标，并比较两份客户端映射前后的文件状态和 SHA-256。已有文件应保持一致，原本不存在的映射文件应无该表记录。核对客户端是否保留未完成标记并按冻结契约阻断后续请求。这个用例的注入条件和数据库检查已在 `run-module.sh` 复用。张永昌接手客户端用例，刘卓明核对状态文件，组长记录接收结果。
