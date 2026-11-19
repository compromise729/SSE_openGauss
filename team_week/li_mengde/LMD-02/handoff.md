# LMD-02 真实密文数据库往返

2026-11-18 · 李孟德 · 交接给张永昌及组长。

## 前置、输入和输出

前置为 LMD-01 和 YZH-01，协议为 SHY-02 v1.0。已校验 YZH-01 交付包 27 个文件，并用包内源码重建动态库。实验在 LZM-01 新隔离实例执行。

| 对象 | 实际版本 |
|---|---|
| SHY-01 源码基准 | `25489f563f25b522d0cd37a6c85865039e3f340f` |
| SHY-02 v1.0 发布记录 | `b6159cf8d0dd6d3d8befa8fd84816d289a7b88d1` |
| LMD-01 服务端输入提交 | `f99e41872fc3f00f9199ed94b5828859e89ed27e` |
| YZH-01 输入提交（交付包发布记录） | `7b99d773e308ec3c06b71654e517f9abfa5e2ccd` |
| 本地工具分支及提交 | `test/LMD-02-roundtrip` · `9fd14f8f4fa9b3a8209c63196bcf9b0c6f9dc5db` |
| 正式 PR 目标 | `integration/SHY-03` |

上表记录本地交付包和原始日志的版本。正式复验使用 LMD-01 合入后的服务端基线 `5074c844a306d97002cc4a30ca80d5a929a6898d` 和 LMD-02 工具提交 `8480117798db475fe5b03b3776406a604dcc6f24`，通过 [PR #7](https://github.com/compromise729/SSE_openGauss/pull/7) 合入 `integration/SHY-03`，合入提交为 `8587978367201f69624e53d6e9b729c2eddb8e28`。后续在与 `c3fa234db4531e8de1aca2314e40a9ea0dec828e` 同源码树的构建上复跑通过。

| 文件 | 内容与用途 |
|---|---|
| [fides-roundtrip-probe.cpp](fides-roundtrip-probe.cpp) | 使用真实 YZH-01 和 Fides，执行绑定写入、索引命中、重插、回查、认证解密和故障注入。 |
| [run-roundtrip.sh](run-roundtrip.sh) | 从脚本位置装载配套文件，编译、复跑 YZH-01 和驱动，在启动的隔离实例加载 LMD-01 SQL。 |
| [roundtrip-fixture.sql](roundtrip-fixture.sql) | 复查版本登记、物理主表和索引封装的长度及摘要，核对定向回查的一致性。 |
| [roundtrip-test.log](roundtrip-test.log) | 原始执行输出，包含真实封装、命中与解密、异常前后摘要。 |
| [LMD-02-roundtrip.patch](LMD-02-roundtrip.patch) | 供组长代导入的 Git 补丁，只含 LMD-02 工具改动。 |
| [results.json](results.json) | 本次模块联测结果、试验版本和构建指纹。 |
| [manifest-sha256.json](manifest-sha256.json) | 本目录报告、测试程序、脚本、夹具和日志的文件摘要。 |

## 同一公开样例的两端结果

客户端先用 YZH-01 随机生成 rid、封装业务行及位置，再用项目现有 SophosClient 生成 ut。通过 libpq 参数绑定提交，数据库主表保存 `rid, data_blob`，索引表保存 `ut, locator_blob`。真实存取逐字节比对通过。

| 客户端认证恢复的业务行 | 服务端主表 rid | 行封装字节 | 首次入库 ut | 位置封装字节 |
|---|---|---:|---|---:|
| `1, apple, red apple` | `8aae1f7509c24ecc8ca009b07a841031` | 108 | `pmNQAID7j4xDs63wIurpdA==` | 96 |
| `2, apple, green apple` | `34ab8f6b9e3472ab98aa7c3090bcf4ab` | 112 | `a7A81qIk/sR0LdsaHDW1TQ==` | 96 |
| `3, banana, yellow banana` | `e3163cf8a6675b1861f6df1fedfe3184` | 116 | `baSCswvtyMXmwRoukh0ZAQ==` | 96 |

这组值来自本次运行，复跑夹具会生成新的 rid 和封装。首轮 `apple` 搜索从索引返回两条 `ut, locator_blob`。客户端认证位置并恢复 rid 和插入操作值 0，生成新 ut 和位置密文写回索引，随后按两个 rid 回查 `rid, data_blob` 并认证恢复两条完整业务行。再次搜索 `apple` 仍命中两条，rid 保持一致。`banana` 命中一条。已消费的旧请求和零计数空请求均返回零行。真实封装 Base64、命中、回查和解密分别对应日志中的 `DATA`、`INDEX`、`HIT` 和 `FETCH` 记录。[roundtrip-test.log](roundtrip-test.log)

## 用例、预期与存取前后摘要

两表摘要按各自主键排序聚合，日志同时记录行数。

| 验证场景和输入 | 预期 | 实测和前后状态 |
|---|---|---|
| DB-02 一批写入三条真实封装 | 两表各新增三行，封装精确保存 | 写入前 0/0，写入后 3/3，两类封装均与客户端提交的原字节一致。 |
| DB-02 三项写入的中间项重复 rid | `23505`，两表回滚 | 返回 `23505`，数据和索引均为 3 行，摘要与失败前一致。 |
| 主表写入后索引 ut 冲突 | `23505`，两表回滚 | 返回 `23505`，两表均为 3 行，摘要与失败前一致。 |
| 同时回查存在和不存在的合法 rid | `21000`，拒绝不完整结果 | 返回 `21000`，两表行数及摘要保持一致。 |
| 命中真实位置后业务行缺失 | 位置仍可认证，回查 `21000` | 事务内模拟主表缺行后，真实令牌仍命中位置。回查返回 `21000`，用例回滚后两表计数和摘要恢复至注入前。 |
| DB-03 真实 Fides 搜索及重插 | `apple` 两次各 2 条，`banana` 1 条，旧令牌和未知词无结果 | 命中数量一致，恢复全部原始字段。主表摘要前后均为 `610cdc26686dfa6a1adc1785a36a9ee5`。重插更新索引摘要，仍为 3 行。 |
| 搜索后追加 `编号 '四', apple, 空串` | 再查命中三条 apple，按原始字节恢复追加行 | 命中 3 条，中文、空格、字面单引号及空 payload 精确恢复。输入直接传入密码模块和参数接口。 |
| 最大合法真实加密行 | 行封装 45,144 字节，认证恢复三个字段的最大长度 | 恢复 16,384、1,024、16,384 字节，重插前后两表各 5 行。审计回查的封装长度和摘要与物理表一致。 |

两种格式错误的内部搜索请求都返回 SQLSTATE `22023`。YZH-01 独立复跑的 226 项断言、8 项随机源故障断言和末行标签损坏夹具也通过，记录与数据库联测位于同一日志。密钥只在客户端临时目录中使用。

## 复跑与接入

先构建 LMD-01 服务端，再用同样的 LZM-01 入口启动另一套新实例。本次联测端口为 55463。进入 `omm` 会话后，使用被测 Git 检出目录下的联测工具和 YZH-01 源码。`vectors` 设为验证交接所附 `public-vectors.tsv` 的绝对路径。脚本在临时目录编译和运行 YZH-01 的共享库与单测，再用同一库运行本轮驱动。[LMD-01 安装步骤](../LMD-01/db-interface.md)

源码与工具走 Git。本报告、SQL 验证夹具、前后摘要、客户端结果和原始日志作为飞书材料。代导入时，组长校验补丁 SHA-256，在已经包含 LMD-01 的联测基准上用 `git -c core.whitespace=cr-at-eol am --keep-cr "$deliverables/LMD-02/LMD-02-roundtrip.patch"` 导入。

```bash
deliverables="$OPENGAUSS_PROJECT_ROOT/team_week/li_mengde"
yzh01="$OPENGAUSS_PROJECT_ROOT/team_week/yan_zihan/YZH-01"
"$ISOLATED_ENTRY" gsql -f "$OPENGAUSS_PROJECT_ROOT/sse_sql/SSE_init.sql"
bash "$deliverables/LMD-02/run-roundtrip.sh" "$ISOLATED_RUN_DIR/socket" "$ISOLATED_PORT" \
  postgres "$ISOLATED_RUN_DIR/client" "$yzh01" "$vectors"
exit
```

实际使用 GCC 7.3、旧 libstdc++ ABI、项目 OpenSSL 1.1 和本地 Unix socket。脚本运行结束删除临时密码目录。日志中保留真实密文供对照，复验时重新生成可认证的同类样例。

张永昌可复用联测驱动的参数绑定、搜索请求构造和结果校验。SQL 夹具用于审计版本、主表、索引和定向回查摘要，正式检索使用认证恢复的 rid 回查。[联测驱动](fides-roundtrip-probe.cpp) · [SQL 审计夹具](roundtrip-fixture.sql) · [接口说明](../LMD-01/db-interface.md)

## 后续验收与接收

张永昌接收字段顺序、样例和测试驱动，在 SHY-03 接入基线验证真实用户 SQL 与客户端输出。用户 SQL 的输入转义和拒绝规则、客户端登记、重开及跨请求中断由相应接入和状态任务验收。故障夹具在同一数据库事务内撤销注入的删除，真实联机流程中的索引取出、重插、回查是独立请求。组长接收并核对本模块证据，完整闭环和候选版本的独立复验分别使用后续集成版本。

有一处可复现性不足。运行脚本从固定的 /opt/og/openGauss-server 取编译头文件，再链接当前测试环境的库。本次核对关键头文件一致，建议后续从被测检出目录取头文件，避免换版本后混用。完整用户 SQL 入口仍由 ZYC-02/03 验收。

SHY-02 v1.0 的位置 AAD 只绑定 Scope，同表内完整合法位置封装的对调仍能通过位置认证。rid 回查和 Fides 检索会暴露部分访问及搜索模式。
