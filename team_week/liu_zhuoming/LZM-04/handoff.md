# LZM-04｜本人验收报告与交接说明

日期：2026-11-20。接手人：组长、张永昌。

**本轮36项真实用例全部通过，VERIFY_EXIT=0；通过范围为已执行的正常继续、故障阻断和受保护目录隔离断言。已解决问题列于第十一节；仍存在的跨端不一致、部分登记及待验证项列于第八节。**

## 一、实际输入与范围

被测分支：main。被测提交：`4a44da0ecb990b3edb584a29dc56b05e650ed77d`。

当前请求搜索使用真实 Fides 和认证加密；索引取出、重插、回查处于数据库事务内。测试通过实际 gsql 用户入口执行；旧式无事务对照使用相同真实 Fides/数据库接口，故意省略当前事务包装，单独记录，不当成当前实现缺陷。

本机已安装程序仍是旧产物且构建生成文件缺失，因此本轮在独立目录恢复生成头/对象列表、复用既有构建缓存并重编当前相关源码。没有覆盖安装程序、安装库或源码目录的生成文件。结果适用于记录的源码与产物指纹；不是干净原生全量发布包验收，后续统一发布仍需复验。

本轮没有修改生产源码。原生请求实现、SQL和隔离工具从已合入当前仓库的文件读取；依赖及其指纹见 [input.json](result/input.json)。

## 二、交付文件作用

| 文件 | 作用 |
| --- | --- |
| [README.md](README.md) | 阅读入口与实际版本。 |
| [env.md](env.md) | Linux用户、实际文件位置、构建与复跑全部命令。 |
| 本文 handoff.md | 逐条验收、完整用例及命令索引、闭环核验、8项缺口、未解决问题与交接。 |
| [人工诊断与测试重建.md](人工诊断与测试重建.md) | 只读诊断和保留现场；只针对可丢弃测试实例的新建步骤。 |
| [preflight/build-current.py](preflight/build-current.py) | 在独立目录补生成文件和虚拟对象清单，调用当前 LZM-03 构建工具构建测试程序。 |
| [preflight/verify.sh](preflight/verify.sh) | 私有挂载、启动、普通用户运行、退出停机和隔离外摘要/权限比较。 |
| [preflight/cases.py](preflight/cases.py) | 36项实际客户端、数据库、进程中断和重启断言；每项独立数据库与状态目录。 |
| [preflight/faults.cpp](preflight/faults.cpp) | 指定状态目录的标记/计数/映射替换错误；实际阶段 SIGKILL、暂停及事件记录。 |
| [preflight/legacy-control.cpp](preflight/legacy-control.cpp) | 真实 Fides旧式非事务重插失败对照，保留标记及索引2→0证据。 |
| [preflight/diagnose.py](preflight/diagnose.py) | 不修改文件的标记、权限、属主和状态摘要诊断，不输出密钥值或密钥摘要。 |
| [result/results.json](result/results.json) | 36项实际结果、请求编号、数据库和状态目录。 |
| [result/fides-counts.json](result/fides-counts.json) | 同一 apple 的计数与结果行数对照。 |
| [result/legacy-control.log](result/legacy-control.log) | 旧式失败后索引为零的原始公开输出。 |
| [result/environment.log](result/environment.log) | 安装程序/库、原状态字节、文件清单/权限/属主均未变的比较结论。 |
| [result/verify.log](result/verify.log)、[result/live.log](result/live.log) | 构建后实际运行摘要及完整公开阶段日志。 |
| result/request-*.stdout、request-*.stderr、request-*.command.json | 每次实际请求的输出、错误事件、完整命令/故障参数与退出码。 |
| [result/closed-loop.json](result/closed-loop.json) | 四项闭环的提交号、输入、预期、实际、命令和证据清单。 |
| result/04-*.commands.json | 各用例核对SQL的实际命令、退出码及结果。 |
| [result/diagnosis-counter.json](result/diagnosis-counter.json) | 以 omm 实际运行只读诊断工具，确认计数保存失败现场仍有未完成标记。 |
| [result/delivery-check.log](result/delivery-check.log) | 交付核对：源码/工具指纹、请求日志和实际命令、文档链接、实例停机和结果一致性。 |

## 三、任务逐条验收

| 任务要求 | 实测与通过依据 |
| --- | --- |
| 旧重插失败夹具、索引可能两行降零、停止展示及重开阻断 | 04-12 真实旧式流程取出2行，重插SQL失败后索引0，标记保留，新进程 incomplete_operation；对照工具只打印数量，不回查业务行。当前入口04-11 同故障回滚2→2，仍停止展示、保留标记、重开阻断。 |
| 标记写失败、计数保存失败、处理中退出、缺钥、第二客户端竞争 | 04-02～06及04-13均通过，详见下表；未补钥、未清标记、未重试失败业务请求。 |
| 客户端重开与服务端重启/setup，保留密钥/计数/映射 | 04-01 每次新进程返回同一完整行集合；实际 gs_ctl 停止再启动，PID改变，单纯重启前后状态字节一致；查询事件含 fides_setup。AEAD/Fides基础密钥与映射字节不变。 |
| 人工诊断与可丢弃测试重建 | 只读工具及完整命令已交付，保留旧现场，复跑生成新目录，不把删除标记或重建当生产恢复。 |
| 新表创建后重开、物理建表失败、映射保存失败 | 04-08～10通过。显式 WITH(sse_storage='ROW_AEAD_V1') 路由；物理DDL失败回滚无表无目录登记；映射失败后部分登记保留并阻断，不自动回退。 |
| 同词插入、首次/重复查询、客户端重开后的状态对照 | 04-01实际记录apple计数2→4→6→8，服务重启后查询为10，每次结果2行；见第四节。 |

## 四、36项用例的输入、预期、实际与原始命令

下列所有用例的实际提交号均为本页开头的完整提交号。全量复跑命令见第九节；详细实际参数在表内命令文件，原始输出按同编号读取。

| 用例/输入 | 预期 | 实际 | 命令及证据 |
| --- | --- | --- | --- |
| 04-01：新建、两条apple、客户端重开/服务重启 | 密钥/映射/版本一致，同结果各一次 | PASS；计数2/4/6/8/10，重启字节不变 | [请求001](result/request-001.command.json)、[请求006](result/request-006.command.json)、[核对SQL](result/04-01-normal-restart-and-counters.commands.json) |
| 04-02：标记rename注入EIO，SELECT及INSERT | 请求或计数变动前失败 | PASS；state_invalid，状态和数据库均未变 | [请求009](result/request-009.command.json)、[请求010](result/request-010.command.json)、[核对SQL](result/04-02-marker-before-effects.commands.json) |
| 04-03：搜索COMMIT后计数rename注入EIO | 不展示，保留现场，新进程阻断 | PASS；本地计数2、数据库已提交、标记保留；重开incomplete_operation | [请求013](result/request-013.command.json)、[请求014](result/request-014.command.json)、[核对SQL](result/04-03-counter-save-failure.commands.json) |
| 04-04：counter_advanced实际SIGKILL | 数据库回滚，标记保留，重开阻断 | PASS；退出信号9，新进程incomplete_operation | [请求017](result/request-017.command.json)、[请求018](result/request-018.command.json)、[核对SQL](result/04-04-real-process-kill.commands.json) |
| 04-05：首客户端begin后暂停，第二客户端访问 | 锁和标记阻止第二客户端修改 | PASS；flock竞争EWOULDBLOCK，第二gsql为incomplete_operation；首客户端释放后完成 | [请求021](result/request-021.command.json)、[请求022](result/request-022.command.json)、[核对SQL](result/04-05-two-clients.commands.json) |
| 04-06：已有数据移走行密钥 | 不生成新钥，两个新进程均拒绝 | PASS；key_invalid，文件和数据库均未变 | [请求025](result/request-025.command.json)、[请求026](result/request-026.command.json)、[核对SQL](result/04-06-existing-data-missing-key.commands.json) |
| 04-07：计数记录截断 | 两个新进程拒绝且不替换坏文件 | PASS；state_invalid，损坏文件保留 | [请求029](result/request-029.command.json)、[请求030](result/request-030.command.json)、[核对SQL](result/04-07-broken-state.commands.json) |
| 04-08：显式新版CREATE，新客户端查unknown | 健康未知词空集，版本识别正确 | PASS；unknown_empty且状态不变 | [请求031](result/request-031.command.json)、[请求032](result/request-032.command.json)、[核对SQL](result/04-08-create-and-reopen.commands.json) |
| 04-09：真实DDL中抛错 | 物理表和服务端登记回滚，重开阻断 | PASS；OBJECTS=0、REG=0，新进程incomplete_operation | [请求033](result/request-033.command.json)、[请求034](result/request-034.command.json)、[核对SQL](result/04-09-physical-create-failure.commands.json) |
| 04-10：建表后本地映射rename失败 | 部分登记不可用，不回退旧明文协议 | PASS；REG=1、本地映射空且标记保留、LEGACY=0；重开阻断 | [请求035](result/request-035.command.json)、[请求036](result/request-036.command.json)、[核对SQL](result/04-10-mapping-save-failure.commands.json) |
| 04-11：当前事务重插触发器失败 | 停止展示并回滚，重开阻断 | PASS；索引2→2，incomplete_operation | [请求039](result/request-039.command.json)、[请求040](result/request-040.command.json)、[核对SQL](result/04-11-current-reinsert-rollback.commands.json) |
| 04-12：旧式无事务取出后重插失败 | 保留2→0证据和未完成标记 | PASS；索引2→0，新进程incomplete_operation | [请求042](result/request-042.command.json)、[请求043](result/request-043.command.json)、[核对SQL](result/04-12-legacy-two-to-zero.commands.json) |
| 04-13：COMMIT后实际chmod状态目录为0500 | 文件创建EACCES，保留旧计数/标记；新进程拒绝 | PASS；计数2，marker保留，重开state_invalid，0500现场不恢复权限 | [请求046](result/request-046.command.json)、[请求047](result/request-047.command.json)、[核对SQL](result/04-13-real-state-unwritable.commands.json) |
| 04-14：缺定位密钥 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；key_invalid；两次新进程均拒绝，状态/数据库不变 | [请求050](result/request-050.command.json)、[请求051](result/request-051.command.json)、[核对SQL](result/04-14-missing-locator.commands.json) |
| 04-15：缺key_id文件 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；key_invalid；两次新进程均拒绝，状态/数据库不变 | [请求054](result/request-054.command.json)、[请求055](result/request-055.command.json)、[核对SQL](result/04-15-missing-keyid.commands.json) |
| 04-16：行密钥截短 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；key_invalid；两次新进程均拒绝，状态/数据库不变 | [请求058](result/request-058.command.json)、[请求059](result/request-059.command.json)、[核对SQL](result/04-16-short-row.commands.json) |
| 04-17：定位密钥截短 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；key_invalid；两次新进程均拒绝，状态/数据库不变 | [请求062](result/request-062.command.json)、[请求063](result/request-063.command.json)、[核对SQL](result/04-17-short-locator.commands.json) |
| 04-18：错误key_id | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；key_invalid；两次新进程均拒绝，状态/数据库不变 | [请求066](result/request-066.command.json)、[请求067](result/request-067.command.json)、[核对SQL](result/04-18-wrong-keyid.commands.json) |
| 04-19：密钥权限0644 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；key_invalid；两次新进程均拒绝，状态/数据库不变 | [请求070](result/request-070.command.json)、[请求071](result/request-071.command.json)、[核对SQL](result/04-19-key-permissions.commands.json) |
| 04-20：损坏映射 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；state_invalid；两次新进程均拒绝，状态/数据库不变 | [请求074](result/request-074.command.json)、[请求075](result/request-075.command.json)、[核对SQL](result/04-20-bad-mapping.commands.json) |
| 04-21：缺计数文件 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；state_invalid；两次新进程均拒绝，状态/数据库不变 | [请求078](result/request-078.command.json)、[请求079](result/request-079.command.json)、[核对SQL](result/04-21-missing-counter.commands.json) |
| 04-22：缺映射文件 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；state_invalid；两次新进程均拒绝，状态/数据库不变 | [请求082](result/request-082.command.json)、[请求083](result/request-083.command.json)、[核对SQL](result/04-22-missing-mapping.commands.json) |
| 04-23：服务端版本改为2 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；state_invalid；两次新进程均拒绝，状态/数据库不变 | [请求086](result/request-086.command.json)、[请求087](result/request-087.command.json)、[核对SQL](result/04-23-server-version.commands.json) |
| 04-24：物理表增加额外列 | SELECT/INSERT均在副作用前拒绝，不补文件或换钥 | PASS；state_invalid；两次新进程均拒绝，状态/数据库不变 | [请求090](result/request-090.command.json)、[请求091](result/request-091.command.json)、[核对SQL](result/04-24-physical-shape.commands.json) |
| 04-25：异常空token | 区别正常未知词，拒绝取索引/推进计数，保留标记 | PASS；protocol_invalid，计数2，重开incomplete_operation | [请求094](result/request-094.command.json)、[请求095](result/request-095.command.json)、[核对SQL](result/04-25-empty-token.commands.json) |
| 04-26：异常空derivation_key | 区别正常未知词，拒绝取索引/推进计数，保留标记 | PASS；protocol_invalid，计数2，重开incomplete_operation | [请求098](result/request-098.command.json)、[请求099](result/request-099.command.json)、[核对SQL](result/04-26-empty-derivation.commands.json) |
| 04-27：标记write失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；未发布标记，文件/数据库不变；无故障新请求正常完成 | [请求102](result/request-102.command.json)、[请求103](result/request-103.command.json)、[核对SQL](result/04-27-marker-write.commands.json) |
| 04-28：标记文件fsync失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；未发布标记，文件/数据库不变；无故障新请求正常完成 | [请求106](result/request-106.command.json)、[请求107](result/request-107.command.json)、[核对SQL](result/04-28-marker-fsync.commands.json) |
| 04-29：标记close失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；未发布标记，文件/数据库不变；无故障新请求正常完成 | [请求110](result/request-110.command.json)、[请求111](result/request-111.command.json)、[核对SQL](result/04-29-marker-close.commands.json) |
| 04-30：标记发布后目录fsync失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；标记保留，新客户端阻断，最终计数2 | [请求114](result/request-114.command.json)、[请求115](result/request-115.command.json)、[核对SQL](result/04-30-marker-dir-fsync.commands.json) |
| 04-31：计数write失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；标记保留，新客户端阻断，最终计数2 | [请求118](result/request-118.command.json)、[请求119](result/request-119.command.json)、[核对SQL](result/04-31-counter-write.commands.json) |
| 04-32：计数文件fsync失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；标记保留，新客户端阻断，最终计数2 | [请求122](result/request-122.command.json)、[请求123](result/request-123.command.json)、[核对SQL](result/04-32-counter-fsync.commands.json) |
| 04-33：计数close失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；标记保留，新客户端阻断，最终计数2 | [请求126](result/request-126.command.json)、[请求127](result/request-127.command.json)、[核对SQL](result/04-33-counter-close.commands.json) |
| 04-34：计数发布后目录fsync失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；标记保留，新客户端阻断，最终计数4 | [请求130](result/request-130.command.json)、[请求131](result/request-131.command.json)、[核对SQL](result/04-34-counter-dir-fsync.commands.json) |
| 04-35：清标记unlink失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；标记保留，新客户端阻断，最终计数4 | [请求134](result/request-134.command.json)、[请求135](result/request-135.command.json)、[核对SQL](result/04-35-clear-unlink.commands.json) |
| 04-36：清标记后目录fsync失败 | 实际命中目标系统调用，错误返回且按阶段保留阻断 | PASS；FAULT_INJECTED已记录，state_invalid；标记保留，新客户端阻断，最终计数4 | [请求138](result/request-138.command.json)、[请求139](result/request-139.command.json)、[核对SQL](result/04-36-clear-dir-fsync.commands.json) |

04-12的旧式对照另调用 `legacy-control`，它不是普通SELECT的第二套生产入口。实际调用由preflight/legacy-control.cpp及cases.py的reinsert(True)定义；本轮命令如下（在隔离会话的omm用户下）：

```bash
SSE_V1_STATE_DIR=/var/tmp/fides-isolated.Xyi3aqSs/evidence/s04_legacy \
/var/tmp/lzm04-build.0PK0r6V3/legacy-control \
'host=/var/tmp/fides-isolated.Xyi3aqSs/socket port=55432 dbname=s04_legacy user=omm' \
/var/tmp/fides-isolated.Xyi3aqSs/evidence/s04_legacy
```

输出见 [legacy-control.log](result/legacy-control.log)。该命令作为本次实际调用记录；完整复跑时由verify.sh自动准备触发器并调用，不在停机后的旧现场重复执行。

04-01服务端重启使用原目录，以omm执行 `gs_ctl stop -D "$run/db/instance" -m fast -w`，再执行 `gs_ctl start -D "$run/db/instance" -Z single_node -w -l "$run/private/restart.log" -o "-p 55432 -c listen_addresses= -k $run/socket"`。本轮run为 `/var/tmp/fides-isolated.Xyi3aqSs`。setup由随后真实搜索入口按Fides要求执行，事件日志已记录。完整复跑无需手动追加这些命令。

计数保存失败说明数据库事务不能覆盖客户端文件；它不等于跨端事务。不能在文件保存失败后重试搜索或自动清标记。映射保存失败的“部分登记”不是可用版本，必须连同标记保留诊断，不宣称两份文件有联合原子提交。

## 五、Fides计数与结果行数

| 阶段 | 本地apple计数 | 结果行数 | 解释 |
| --- | --- | --- | --- |
| 插入完成 | 2 | 表中2条 | 两次真实Fides更新令牌生成。 |
| 首次查询完成 | 4 | 2 | 取出两条索引并为两个有效rid生成新令牌重插。 |
| 重复查询完成 | 6 | 2 | 再次重插两条，业务表未新增行。 |
| 新客户端重开查询完成 | 8 | 2 | 同密钥和原计数加载后继续，计数不重置。 |
| 服务端重启后setup查询完成 | 10 | 2 | 重启本身未改变计数；之后查询重插再推进2次。 |

计数记录更新令牌推进历史，不等于当前业务行数。不是Mitra的r加一、c归零规则。本轮每次返回完整集合 `1|apple|red apple`、`2|apple|green apple`，各一次。

## 六、放行条件与原始证据

本轮实际证据目录 `/var/tmp/lzm04-evidence.jfMpDMvm`；个人实例 `/var/tmp/fides-isolated.Xyi3aqSs`；构建目录 `/var/tmp/lzm04-build.0PK0r6V3`。实例已停止。每项状态位于个人目录evidence/s04_*，对应数据库与实际请求编号见results.json。

原安装bin/lib文件清单与SHA-256一致，原sse_storage和sse_server_storage文件清单、字节摘要、权限及属主一致。比较文件含密钥摘要，保留本机*.private，不进入公开交付；公开结果仅保存比较结论。

正常状态可重启继续；未完成标记、缺钥和状态损坏阻断；隔离外受保护文件未变。因此本轮36项场景的行为断言通过。此结论不表示已消除跨端不一致、部分版本登记等问题；这些未解决项见第八节。适用范围是所记录的当前main与独立测试产物，后续统一原生发布或源码更新后按组长版本重新验证。

## 七、交接与保留限制

交给组长、张永昌：计数保存后数据库与文件不一致现场、真正SIGKILL和并发证据、建表部分登记现场、旧重插2→0与当前事务2→2对照。

不提供密钥恢复、自动补索引、跨端原子提交或安全重试；不完成多库多表计数命名空间重构。故障实例只用于诊断，重新跑用例建立新的可丢弃环境，不将重建描述为生产恢复。

## 八、未解决问题、影响与后续修改建议

本节只保留当前仍存在的问题、能力限制及未验证项，源码位置对应被测main提交 `4a44da0ecb990b3edb584a29dc56b05e650ed77d`。已完成的检查、补测和工具问题移至第十一节；原问题编号保留便于核对证据。

| 编号及问题 | 本轮做到哪里、证据与当前措施 | 仍未解决的影响 | 后续修改与复验建议 |
| --- | --- | --- | --- |
| U01 数据库与本地计数不一致（已实测） | 04-03计数替换失败、04-13真实权限拒绝，均保留标记并阻断新客户端。<br>定位：[row_request_v1.cpp](../../../src/common/interfaces/libpq/row_request_v1.cpp) 的execute_request，第483行先COMMIT，第484行才save_counters；[row_state_v1.cpp](../../../src/common/interfaces/libpq/row_state_v1.cpp) 第765～781行只保存本地计数。日志：[013](result/request-013.stderr)、[046](result/request-046.stderr)。 | 数据库的新索引已经提交，但本地apple计数仍为2，当前只阻断、未修复不一致。operation.v1主要记录操作ID/作用域/种类，没有完整前后计数、重插内容和服务端回执，不能直接安全重试。INSERT第430～432行也有同类顺序窗口，本轮失败实测针对搜索。 | 状态、客户端和SQL负责人共同扩展操作日志及按操作ID确认/幂等处理的协议，明确每个提交窗口的恢复决策；测试COMMIT前后及本地保存前后中断。只增加fsync或把计数保存提前，不能解决跨端原子性：提前保存仍可能遇到数据库提交失败。 |
| U02 建表登记可能只保存一半（已实测） | 04-09服务端DDL异常会回滚；04-10服务端已登记1条，本地版本文件已发布、sse_table.mat为空，标记阻断且未回退旧明文。<br>定位：[row_request_v1.cpp](../../../src/common/interfaces/libpq/row_request_v1.cpp) 第419～420行先sse_v1_create再register_table；[row_state_v1.cpp](../../../src/common/interfaces/libpq/row_state_v1.cpp) 的register_table第679行先写版本、第680行再写映射。证据：[04-10核对SQL](result/04-10-mapping-save-failure.commands.json)、[035](result/request-035.stderr)。 | 映射保存失败不会撤销已经创建的服务端表，也不会撤销已发布的本地版本。现场仍不完整，只是不能继续使用。启动时标记阻断；即使越过标记，load_local第359～366行也会拒绝映射与版本不符。 | 共同设计建表意图、服务端结果、本地版本发布及中断核对/补偿状态机。分别在服务端成功、版本发布、映射发布、清标记时注入失败；明确怎样处理已创建的表。合并两份本地文件也不能单独解决服务端与本地联合提交。 |
| U03 旧式无事务重插失败会删空索引（旧式对照已实测） | 04-12旧式对照2→0且重开阻断；04-11当前事务入口回滚为2→2。<br>定位：[legacy-control.cpp](preflight/legacy-control.cpp) 第23～29行不使用外层事务，先取出再单独重插；[Server_Sophos.cpp](../../../src/gausskernel/process/tcop/Server_Sophos.cpp) 第131行调用sse_v1_take；[SSE_v1.sql](../../../sse_sql/SSE_v1.sql) 第145～146行执行DELETE RETURNING。证据：[legacy-control.log](result/legacy-control.log)。 | 旧式取出的删除已经提交，后续重插失败无法撤销；业务表2行而索引0行。当前execute_request第442行已有SearchTransaction，因此不能把旧式对照写成当前入口仍会2→0。历史丢失索引没有恢复，setup不能补回。 | 张永昌盘点绕过新版事务/标记的调用入口，统一接入并回归；另行设计有可信输入的历史索引诊断/重建方案。不靠重新setup或清标记继续。本轮没有逐一审计所有旧入口，不能声称都已修复。 |
| U04 业务编号ind没有唯一/覆盖语义（结构限制，重复ind本轮未测） | 04-01证明正常查询重插没有新增业务行。<br>定位：[row_request_v1.cpp](../../../src/common/interfaces/libpq/row_request_v1.cpp) 第391～394行每条输入都生成新rid，没有按ind查重；[SSE_v1.sql](../../../sse_sql/SSE_v1.sql) 第58～60行仅以rid为主键，ind在整行密文内。 | 相同ind再次插入可以获得不同rid，现有物理主键无法排除两条业务记录。rid去重不等于ind去重，旧表重复数据和删除再插问题未修复；本轮只用了不同ind。 | 先确定追加、唯一拒绝或覆盖语义，再由客户端/SQL负责人设计与加密字段相容的唯一标识和更新协议，补重复ind、覆盖失败及旧数据审计。不能直接把随机rid换成ind便宣称兼容。 |
| U05 多库多表计数作用域未重构（明确限制） | 独立目录及04-05互斥生效。<br>定位：[row_state_v1.h](../../../src/include/libpq/row_state_v1.h) 第22行Counters是字符串到整数映射；[row_state_v1.cpp](../../../src/common/interfaces/libpq/row_state_v1.cpp) 第43～48行仅一个database_oid/table，第379行按关键词存计数；[row_state_catalog.cpp](../../../src/common/interfaces/libpq/row_state_catalog.cpp) 第55～58行直接拒绝多张新版表。 | 同一目录不是可支持多库多表的计数空间。表作用域认证只能拒绝错误上下文，不能把不同表的apple计数拆开。每个夹具使用独立目录、第二客户端被拒绝，都不等于完成多库多表并发隔离。 | 状态负责人联合客户端/SQL负责人，将完整Scope纳入计数、登记、映射、锁和密钥关联，设计格式版本与迁移，再测同词跨表、同名跨库及并发。 |
| U06 密钥恢复、轮换与一致备份没有实现（能力缺口） | 04-06缺钥key_invalid且不补建；04-01使用同钥正常重开。<br>定位：[row_aead_v1.cpp](../../../team_week/yan_zihan/YZH-01/src/row_aead_v1.cpp) 的initialize_keys第359～391行仅初始化空目录，load_keys第394～412行只加载已有密钥，第329～331行核对封装key_id；[row_state_v1.cpp](../../../src/common/interfaces/libpq/row_state_v1.cpp) 第357行绑定环境key_id，第402～405行还绑定数据库OID和实例目录。 | 缺钥拒绝不是找回密钥。无可信备份时，生成新文件不能解密旧数据；复制数据库/密钥到新位置也不能假定通过原环境身份检查。当前接口没有实现多版本轮换和备份迁移流程。 | 密码、状态和SQL负责人设计密钥托管、key_id版本选择、轮换及数据库/状态/密钥一致备份点，明确环境身份迁移规则；用可丢弃数据演练原位恢复、迁移及轮换中断后再交付恢复步骤。 |
| U07 其他SSE方案历史故障未逐一复验（未验证） | 本轮36项只测新版Fides及旧式Fides对照。<br>可定位的是测试范围：[cases.py](preflight/cases.py) 文件末尾的用例列表没有其他方案的用例；未复现具体故障，不能据算法名指认根因函数。 | Diana崩溃、Mitra词处理/JOIN、LEFT/RIGHT JOIN语义、Orion空结果/重插、范围边界、FBDSSE/CloseFB/Dual边界故障仍缺本轮证据。Fides通过不能推导所有算法正常。 | 组长安排对应负责人使用历史输入复现，保存调用栈/日志后再定位错误行和修复；共享代码变化做受影响回归。复现前继续写未验证，不把任务书描述当作本轮已确认错误。 |
| U08 多关键词、范围、密文JOIN及旧数据迁移未专项实现或验收（能力限制） | 当前仅验证固定结构CREATE、VALUES INSERT和单关键词等值SELECT。<br>定位：[row_request_v1.cpp](../../../src/common/interfaces/libpq/row_request_v1.cpp) 第355～380行限定SQL形式，Parser::end第219～220行拒绝剩余语法，第455行仅接受插入类型的位置载荷。 | 旧协议存在相似SQL分支，不等于新版认证加密协议支持这些能力。旧明文数据不能仅修改版本登记就变成新版密文数据。 | 客户端、密码与SQL负责人先设计查询协议、认证上下文及迁移校验，再扩展解析/服务端能力；补认证失败、部分迁移中断及禁止回退用例。本轮没有这些能力的通过结论。 |
| U09 TCP/TLS、性能及完整安全证明未验收（验证范围缺口） | 本轮使用Unix socket并关闭TCP。<br>定位：[LZM-01/isolated.sh](../LZM-01/isolated.sh) 第244～245行配置socket及空listen_addresses；[row_state_catalog.cpp](../../../src/common/interfaces/libpq/row_state_catalog.cpp) 第40～41行要求Unix socket。这里是连接范围定位，不是已复现TLS错误。 | 没有TCP/TLS握手、证书或传输加密证据；业务认证加密不代表启用TLS。没有新链路压测或完整安全证明，不能沿用其他算法性能。 | 独立设计部署连接及实例身份校验，验收证书/协议；固定硬件数据集压测并评审威胁模型。不能只删除Unix socket检查就宣称支持安全TCP接入。 |
| U10 构建依赖本机缓存、硬编码路径及库版本标识警告（已确认） | 独立验证产物可运行，但不是干净原生安装包。<br>定位：[build-current.py](preflight/build-current.py) 第5～39行补生成文件并按脚本文本/make输出恢复对象清单；[LZM-03/build-live.py](../LZM-03/preflight/build-live.py) 第36～48行复用既有对象，第58～68行组合已有库；[isolated.sh](../LZM-01/isolated.sh) 第18～20行固定配置/挂载目标，[Client_Sophos.cpp](../../../src/common/interfaces/libpq/Client_Sophos.cpp) 第10行固定基础密钥目录。<br>警告产生于[startup.cpp](../../../src/bin/psql/startup.cpp) 第237～243行：libpqVersionString与DEF_GS_VERSION不同，见[001日志](result/request-001.stderr)。 | 其他机器缺缓存、布局变化或make输出变化，可能无法按适配脚本构建。警告说明版本标识不同，尚未完整追溯配置/安装来源；它既不能直接证明ABI已损坏，也不能证明全部兼容。本轮私有挂载没有消除这些问题。 | 组长维护原生生成依赖、完整对象及可配置路径，在无旧缓存环境生成同来源、可追溯的gsql/libpq/服务端产物，再复跑36项并追溯版本标识。环境安装变更另由用户决定；不升级现有libpq或隐藏警告作为本次修复。 |
| U11 系统级、复合故障及全部提交窗口尚未验证 | 定位：[faults.cpp](preflight/faults.cpp)每次请求只注入指定单次I/O错误；[cases.py](preflight/cases.py)覆盖当前36项场景。 | 尚无ENOSPC、只读文件系统、机器掉电、内核崩溃、每个提交窗口中断及多重持久化失败的实测结果；不能据单次EIO或SIGKILL推导这些场景通过。 | 针对这些剩余场景设计专门夹具/环境，记录错误返回、文件持久化、重启与副作用；单次持久化负例已完成，见第十一节。 |
| U12 其他成员环境的复跑尚未验证 | 当前36项结果来自本机隔离实例，见[results.json](result/results.json)和[verify.log](result/verify.log)。本轮尚未验证组长、张永昌环境的实际运行。 | 不能据本机通过推导其他机器的依赖、路径、权限和隔离运行均正确。 | 各成员按env.md新建个人实例复跑，核对进程/socket/两侧状态目录和环境外摘要；环境差异或失败按实际情况处理，保留日志。 |

### 处理顺序与证据范围

后续优先协同处理U01/U02协议与恢复决策，再完善U10原生发布、U11系统/复合故障和U12其他成员环境复验。U03历史索引、U04业务语义、U05作用域、U06密钥管理、U07～U09能力与专项验收分别交相应负责人接续。这是后续建议分工，不表示已发送消息或已被接收。

本次个人目录 `/var/tmp/fides-isolated.Xyi3aqSs`、外层证据 `/var/tmp/lzm04-evidence.jfMpDMvm`。完整状态目录、请求编号、输入/命令和结果见 [result/results.json](result/results.json) 与 [闭环核验（第九节）](#closed-loop)。隔离外未变的证据范围是安装bin/lib字节及原两侧状态的清单、字节、权限和属主，不是整机文件全量审计。含密钥摘要的比较文件只留本机private目录。

<a id="closed-loop"></a>

## 九、交付前闭环核验

本次使用现有验证工具新建隔离实例并重新执行36项，全部PASS，VERIFY_EXIT=0，实例已停止。result/只保存本次最新结果；原实验目录及异常现场仍保留在本机。

### 9.1 实际执行命令

Windows PowerShell先打开Linux root终端：

```powershell
wsl.exe -d CentOS7 -u root
```

本机Linux root终端执行：

```bash
cd /opt/og/openGauss-server
source /home/omm/.sse_opengauss_env
source /opt/rh/rh-python36/enable
tools=/opt/og/openGauss-server/team_week/liu_zhuoming/LZM-04/preflight
build=/var/tmp/lzm04-build.0PK0r6V3
log=$(mktemp /var/tmp/lzm04-closure.XXXXXXXX.log)
rc=0
bash "$tools/verify.sh" "$build" > "$log" 2>&1 || rc=$?
cat "$log"
printf '退出码=%s\n日志=%s\n' "$rc" "$log"
```

tools表示工具文件夹；build是本机已成功构建的独立测试产物目录。其他机器先按 [env.md](env.md) 构建，把build设为自己的实际成功目录。root负责私有挂载；数据库、请求和权限故障均由omm执行。每次复跑新建个人目录，不复用或覆盖本轮故障现场。

每条实际请求的可执行程序、参数、SQL、故障环境变量、退出码及事件序列保存在 `result/request-NNN.command.json`；同编号stdout/stderr就是对应输出。`result/04-NN-名称.commands.json`保存只读核对及建夹具的原始SQL命令与结果。初始化程序的完整命令、退出码和INITIALIZE_STATUS位于results.json中每项initialization字段。这里的故障注入只用于可丢弃个人实例。

### 9.2 四项闭环对照

| 核验项 | 预期 | 实际结果和证据 | 判定 |
| --- | --- | --- | --- |
| C1 新环境、映射、密钥、版本与环境证据 | 空目录真实初始化，密钥元信息和key_id匹配，映射和两端版本一致；保存内外对照 | 36项initialization均记录匹配；真实初始化退出0。每项本地清单、权限、属主在final_state，密钥摘要仅存个人private目录；隔离外三项比较通过。见[results.json](result/results.json)、[environment.log](result/environment.log)。 | 已执行PASS |
| C2 客户端重开与服务重启/setup | 同密钥与原计数继续；集合和重复次数一致 | 04-01使用新gsql进程及实际gs_ctl停/启；PID改变，重启本身状态字节未变；查询含fides_setup，编号1/2各一次，计数2→4→6→8→10。见[fides-counts.json](result/fides-counts.json)、[request-006.stderr](result/request-006.stderr)。 | 已执行PASS |
| C3 七类实际链路故障 | 标记、不可写、重插、中断、缺钥、登记失败、第二客户端均有真实请求证据 | 第四节逐项列明。新增04-13以omm实际chmod为0500并验证EACCES，不再以rename注入代替权限不可写。 | 已执行PASS |
| C4 故障现场和重开持续阻断 | 原文件不被清空、密钥不补建、失败请求不盲目重试；诊断与测试新建分开 | 未完成标记场景新进程incomplete_operation；缺钥key_invalid；截断/0500权限state_invalid。原现场及私有摘要保留，实例shut down。见[diagnosis-counter.json](result/diagnosis-counter.json)、[diagnosis-readonly.json](result/diagnosis-readonly.json)、[pg-control.txt](result/pg-control.txt)、[人工诊断与测试重建.md](人工诊断与测试重建.md)。 | 已执行PASS |

C4的新进程查询用于确认“继续被拒绝”，不是自动恢复或重试失败业务。标记写入失败发生在任何修改前，不要求人为补一个未完成标记；并发首客户端正常完成后可按契约清除它自己的标记。其余异常不自动清除。

### 9.3 证据位置及未验证项

本次个人目录 `/var/tmp/fides-isolated.Xyi3aqSs`，外层证据 `/var/tmp/lzm04-evidence.jfMpDMvm`，实例已停止。每个场景状态目录见results.json的state_dir。文件摘要（含密钥摘要）保存在个人目录 `private/lzm04/snapshot-*.json.private`，隔离外对照保存在外层 `*.private`，均只留本机。公开交付保存比较结论、元信息、命令和公开样例输出。

**已补齐：** 04-27～36的标记/计数write、close、文件及目录fsync、清标记失败。实现使用write/fsync，不额外经过stdio fflush。**未验证：** ENOSPC、只读文件系统、整机掉电或内核崩溃、每个提交窗口中断、复合持久化故障、各接手人机器独立复跑。0500权限测试不等于只读文件系统测试；SIGKILL不等于整机掉电。已有其他任务的模块测试也不替代本次真实链路证据。

本轮源码/产物指纹见 [input.json](result/input.json)，四项机器可读对照见 [closed-loop.json](result/closed-loop.json)。12类未解决问题见 [本人报告兼交接说明](handoff.md) 第八节；保留标记和阻断不能修复跨端不一致、部分登记或历史丢失索引。

<a id="gap-review"></a>

## 十、任务书剩余缺口与接续工作

任务书缺口2、3、5的检查与当前负例证据、缺口7的正常重开/重启，以及缺口8的工具移动与仓库外调用，均已完成并移到第十一节。下表仅列仍需继续处理的部分，保留原任务书编号。

| 原缺口 | 当前仍缺的部分 | 定位及证据 | 接续工作 |
| --- | --- | --- | --- |
| 1 新表结构与客户端登记 | 协议和失败阻断已有；剩余是服务端与本地登记缺少联合提交/中断恢复。 | 04-10部分登记现场；row_request_v1.cpp第419～420行和row_state_v1.cpp第679～680行，详见U02。 | 状态、客户端与SQL负责人共同设计登记状态机、确认及补偿流程。 |
| 4 重插失败与历史索引删空 | 当前入口的事务回滚和阻断已验证；旧式无事务入口、历史丢失索引未统一修复或恢复。 | 04-12索引2→0；legacy-control.cpp第23～29行，详见U03。 | 张永昌盘点旧入口；另行设计可信索引诊断/重建，不能把setup当恢复。 |
| 6 持久化失败、中断与并发 | 单次错误处理与互斥已验证；剩余是跨端恢复，以及系统级/复合故障、所有提交窗口的验证。 | 04-03/13两端不一致；U01、U02和U11。 | 协同定义恢复协议；对未执行故障分别建立测试，不重复把已通过的单次错误列为待补。 |
| 8 目录与交接复跑 | 工具复制和仓库外启动已通过；编译挂载路径、基础配置、构建缓存依赖及其他成员环境验证仍需处理。 | isolated.sh第18～20行、Client_Sophos.cpp第10行；U10、U12。 | 组长完善原生发布和配置；各成员按env.md复跑并核对隔离与摘要。 |

其他仍未完成的业务编号语义、多库多表计数、密钥恢复/轮换、其他算法与查询能力、传输/安全/性能验收，统一见第八节，不在此重复列问题。

当前结论和复现依据均为本轮36项实测及result/input.json记录的源码、工具和产物指纹。

<a id="resolved"></a>

## 十一、已解决问题

以下只列任务要求中的功能、防护和环境工具问题，按实际处理方式与实测结果说明。当前结果为36项PASS、139次真实gsql请求、VERIFY_EXIT=0；各用例输入、命令、预期、实际和原始输出见第四节及 [results.json](result/results.json)。

| 已解决问题 | 处理方式与文件位置 | 实测结果及证据 | 对应范围 |
| --- | --- | --- | --- |
| 新版建表、版本登记及重开识别 | 现有execute_request、register_table及启动检查配合；定位row_request_v1.cpp第419～423行、row_state_v1.cpp第665～683行。 | 04-08创建后新客户端正确识别；04-09物理DDL失败回滚；04-10登记失败保留标记并禁止旧明文回退。 | 原缺口1的协议/识别/阻断部分已完成；两端登记恢复仍见U02。 |
| 已有数据缺钥、短钥、错误key_id及权限错误的拒绝 | cases.py的reject_corrupt在真实SELECT和INSERT中分别触发，检查无文件补建/换钥；现有startup和状态/密码模块返回失败。 | 04-06、04-14～19均key_invalid，两个新客户端均拒绝；状态与数据库快照不变。 | 原缺口2的拒绝及失败状态处理已完成；密钥恢复/轮换仍见U06。 |
| 健康未知词、坏状态及异常空令牌的分类处理 | faults.cpp的search_request钩子只清空真实Fides生成结果的指定字段，cases.py的empty_token检查发送和计数阶段。 | 04-08正常unknown_empty；04-07坏状态state_invalid；04-25/26异常空字段protocol_invalid，无取索引/计数推进，标记保留、重开阻断。 | 原缺口3已完成。 |
| 自动前置检查与映射、版本、物理结构异常拒绝 | 现有startup.cpp连接前后检查、row_state_v1.cpp拿锁重查；reject_corrupt补计数、映射、远端版本和物理结构负例。 | 04-20～24的SELECT/INSERT均state_invalid，发生在副作用前；状态与数据库未变。 | 原缺口5已完成；检查通过不代表跨端数据全面一致。 |
| 标记持久化失败时在状态修改前阻断 | 现有begin_checked_operation先持久化标记；faults.cpp及cases.py覆盖替换、write、文件fsync、close和目录fsync失败。 | 04-02、04-27～30均拒绝；未取索引/推进计数。未发布标记的失败保留干净状态，随后无故障请求可成功；已发布标记则重开阻断。 | 单次标记持久化失败的阻断已验证。 |
| 计数保存及清标记失败后的错误返回与阻断 | 04-31～36注入计数write/fsync/close、目录同步及清标记unlink/目录同步错误，必须实际记录FAULT_INJECTED；验证现有save_counters和clear_marker错误返回。 | 全部state_invalid且停止展示；按阶段保存旧/新计数并保留标记，新客户端继续阻断；单次清标记目录fsync失败后成功恢复标记。 | 单次持久化错误的测试已完成；跨端恢复见U01，复合/系统故障见U11。 |
| 进程中断后持续阻断与第二客户端互斥 | 现有操作标记与目录flock；cases.py真实SIGKILL、两进程竞争、重开前后字节对比。 | 04-04中断后标记保留且重开拒绝；04-05独立锁探针EWOULDBLOCK，第二gsql为incomplete_operation且无副作用。 | 原缺口6的已测中断/互斥/阻断部分已完成。 |
| 同密钥、计数和映射下的客户端重开与服务重启 | cases.py的normal使用新gsql进程、真实gs_ctl停/启、原目录与原密钥；搜索按现有实现setup。 | 04-01每次返回编号1和2各一次；计数2→4→6→8→10，重启本身不改状态，密钥及映射保持一致。 | 原缺口7的正常流程已完成。 |
| 工具移动后的资源定位与仓库外调用 | verify.sh复制isolated.sh及preflight到EVIDENCE_DIR/isolated-tools，显式传OPENGAUSS_PROJECT_ROOT，从/var/tmp启动。 | 同一生命周期内36项全部通过，entry实际位置见[verify.log](result/verify.log)。 | 原缺口8的工具移动/仓库外调用已完成；编译与环境依赖仍见U10。 |

实际代码增量仅为 `preflight/faults.cpp`、`preflight/cases.py`、`preflight/verify.sh`。上述状态防护已有实现，本次补齐真实请求验证后均通过，没有发现需要修改生产状态源码才能通过本批用例的错误，因此未改动startup.cpp、row_state_v1.cpp或密码/SQL源码。


## 十二、最新结果与复现确认

任务书要求每项保留实际版本、输入、命令、预期/实际结果和原始证据，失败保留现场；没有要求交付目录收录每次补测的重复历史副本。本次以未修改的现有preflight工具重新执行36项，139次真实请求全部完成，VERIFY_EXIT=0。运行指纹见result/input.json，实际命令及输出见第四节及result/。

交付result/仅保存本次最新结果，已移除previous-12-cases/和previous-13-cases/两份公开副本。旧个人实验目录和异常现场没有删除；本轮个人目录、原始证据及私有摘要位置见第六、九节。复现要求相同的行为断言、结果集合、计数推进及隔离结论，不要求随机生成的目录/PID/密钥/密文字节相同。具体步骤及判据见env.md。
