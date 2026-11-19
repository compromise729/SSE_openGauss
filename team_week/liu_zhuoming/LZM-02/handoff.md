# LZM-02｜任务实现与交付文档

报告日期为 2026-11-18，本轮在拉取 main 后于 WSL CentOS7 完成复测。负责人为刘卓明，接手人为张永昌和组长，任务截止时间为 2026-11-18 12:00。

## 一、任务要求与本次工作

本任务承接 SHY-02 v1.0 契约，为新版 Fides 提供状态检查、目录互斥锁、操作标记和可靠的本地文件保存接口。调用方根据接口返回的状态决定是否继续执行，终端检查器输出 JSON 和退出码，测试脚本通过断言判断结果。

| 任务要求 | 本次实现内容 |
| --- | --- |
| 扩展状态检查 | 在关键词计数与表映射之外，加入新版密钥文件元信息、部署标识、数据库身份、本地表版本和服务端登记校验。 |
| 提供契约接口和持久化标记 | 实现 `preflight`、`begin_operation`、`complete_operation`、`register_table`、`load_table_version`。操作开始前写标记，成功后清除，中断后重启继续阻断。 |
| 检查计数和映射保存结果 | 使用同目录临时文件、完整写入检查、文件同步、关闭检查、原子替换和父目录同步。 |
| 阻止并发修改 | 对同一状态目录取得非阻塞独占锁，第二个操作返回 `busy`。 |
| 覆盖异常并保留实测输出 | 验证截断、缺失、不可读、不可写、缺钥、环境或版本冲突、保存失败、未完成标记、正常完成和第二进程竞争。 |
| 把新版表登记纳入操作 | 建表持有 `create` 标记，核对服务端建表结果后写本地两份登记，启动时重新校验。 |

实现依据为已合入 main 的 SHY-02 v1.0 契约和共用头文件。本地接口与 `team_week/leader/SHY-02/include/sse_v1_contract.hpp` 完全一致。状态与启动改动按任务书分工交给组长和张永昌。

本次被测输入为 main 提交 `7bace5b2182552f1e2fe5865ac66d58812435510` 加 LZM-02 源码，契约为 SHY-02 v1.0。用户已完成正式提交，输出提交号为 `df1a7cc22dc03305aa9aaa463e210c2fb82178fb`，提交分支为 `feature/LZM-02-state`，PR 目标为 `integration/SHY-03`。已读取本地提交对象并逐个核对，提交清单中的 20 个源码和工具文件与本次交付版本完全一致。

本轮以源码和暂存区指纹确认 main 的工作区源码保持原样，更新范围为 report 与 team_week 文档和实测结果。模块及独立检查器从当前源码编译，gsql/libpq 以现有构建产物运行。指纹与结果见 [current-input.json](result/current-input.json)。

### 正式合入与初始化补修

上文和 `result/current-input.json` 记录原版自测。LZM-02 原版通过 [PR #8](https://github.com/compromise729/SSE_openGauss/pull/8) 合入，合入提交为 `9348fab0f2f3682a2be3c22f17c8e3bfe6e71007`。真实密钥初始化补修为 `ef83bc7819df0121d7445d3532936ea19b9bf7fd`，通过 [PR #10](https://github.com/compromise729/SSE_openGauss/pull/10) 合入 `integration/SHY-03`，合入提交为 `c3fa234db4531e8de1aca2314e40a9ea0dec828e`。CentOS 复验通过首次生成密钥、重开读回原密钥、中途失败后拒绝启动和重复初始化，以及状态、桥接和数据库登记测试。LZM-03 以该集成提交为准。

## 二、交付文件及作用

### Git 提交的源码，共 10 个文件

以下是逐文件允许清单，路径从项目根目录算起。第三列说明本次改动的函数或代码位置。

| 文件 | 函数或代码位置 | 本次作用 |
| --- | --- | --- |
| [src/bin/psql/startup.cpp](../../../src/bin/psql/startup.cpp) | `validate_local_startup、validate_startup` | 新版启动先校验本地标记和密钥元信息，再核对同连接的服务端版本。 |
| [src/common/interfaces/libpq/Makefile](../../../src/common/interfaces/libpq/Makefile) | `OBJS` | 纳入 row_state_v1.o 和 row_state_catalog.o。 |
| [src/common/interfaces/libpq/Client_Sophos.cpp](../../../src/common/interfaces/libpq/Client_Sophos.cpp) | `writeDictionary、appendDictionaryEntry、四个 get_and_increment 重载` | 新版保存接入活动操作，保存失败通过异常向调用方传播，推进从持锁计数快照计算。 |
| [src/common/interfaces/libpq/SSE_map.cpp](../../../src/common/interfaces/libpq/SSE_map.cpp) | `save_v1_mapping 及三个映射保存入口` | 核对 create 操作和列映射，然后检查服务端并持久化两份本地登记。 |
| [src/common/interfaces/libpq/sse_common.cpp](../../../src/common/interfaces/libpq/sse_common.cpp) | `require_v1_state、字典保存和 get_and_increment` | 共用计数入口接入同一套锁、标记和保存流程。 |
| [src/common/interfaces/libpq/row_state_v1.cpp](../../../src/common/interfaces/libpq/row_state_v1.cpp) | `preflight、begin_operation、complete_operation、durable_write 等` | 实现状态解析、环境绑定、目录锁、标记、原子替换和错误分类。 |
| [src/common/interfaces/libpq/row_state_catalog.cpp](../../../src/common/interfaces/libpq/row_state_catalog.cpp) | `read_catalog、relation_shape` | 同连接读取 OID、实例目录、表版本、列属性及 rid/ut 单列主键。 |
| [src/include/libpq/sse_v1_contract.hpp](../../../src/include/libpq/sse_v1_contract.hpp) | `SHY-02 共用类型和公开接口` | 使用已合入 main 的 SHY-02 v1.0 签名。 |
| [src/include/libpq/row_state_v1.h](../../../src/include/libpq/row_state_v1.h) | `状态模块内部接入声明` | 声明初始化、启动校验、持锁计数与原入口适配函数。 |
| [src/include/libpq/row_state_catalog.h](../../../src/include/libpq/row_state_catalog.h) | `read_catalog 声明` | 声明基于 PGconn 的服务端读取适配器。 |

### Git 提交的可复跑工具，共 10 个文件

工具路径均位于 `team_week/liu_zhuoming/LZM-02/preflight/`。

| 文件 | 作用 |
| --- | --- |
| [preflight/build-checker.sh](preflight/build-checker.sh) | 编译独立 C++ 检查器。 |
| [preflight/check-state.cpp](preflight/check-state.cpp) | 输出 JSON 和 0/1/2/3 退出码。 |
| [preflight/fixtures.h](preflight/fixtures.h) | 提供公开合成夹具。 |
| [preflight/state-cases.cpp](preflight/state-cases.cpp) | 状态、锁、标记、保存失败及中断测试。 |
| [preflight/verify-state.sh](preflight/verify-state.sh) | 执行 STATE-01～12。 |
| [preflight/catalog-fixture.cpp](preflight/catalog-fixture.cpp) | 创建隔离数据库物理表与登记夹具。 |
| [preflight/verify-catalog.sh](preflight/verify-catalog.sh) | 执行 CATALOG-01～07，必需产物缺失即失败。 |
| [preflight/bridge-cases.cpp](preflight/bridge-cases.cpp) | 调用实际 libpq 中的原有保存入口验证接入。 |
| [preflight/verify-bridges.sh](preflight/verify-bridges.sh) | 执行 BRIDGE-01～04。 |
| [preflight/verify-prerequisites.sh](preflight/verify-prerequisites.sh) | 执行 CATALOG-08 的三个缺产物/权限分支。 |

### 飞书提交的说明和结果

| 本地文件 | 飞书材料用途 |
| --- | --- |
| [README.md](README.md) | 工具文件夹的阅读入口。 |
| [state-api.md](state-api.md) | 状态接口、调用顺序和持久化语义。 |
| [env.md](env.md) | 隔离复跑及初始化、恢复说明。 |
| 本文档 handoff.md | 任务要求、文件清单、实现方法、实测结果及正式交接记录。 |
| [result/state.log](result/state.log)、[result/bridges.log](result/bridges.log) | 状态模块和原保存入口的实测输出。 |
| [result/catalog.log](result/catalog.log)、[result/prerequisites.log](result/prerequisites.log) | 服务端校验、启动阻断和必需产物缺失测试输出。 |
| [result/current-input.json](result/current-input.json) | 本次输入提交、源码与产物 SHA-256、用例结果和证据位置。 |

这些文档和结果作为飞书正文或附件交付，保留本地副本。Git 按上面的 20 个源码和工具文件逐项选择。


## 三、各项任务的实现方法

### 1. 状态、密钥元信息与环境校验

状态目录由 `SSE_V1_STATE_DIR` 指定，默认使用隔离挂载后的 `/opt/og/openGauss-server/sse_storage`。模块要求目录归运行用户所有、权限为 `0700`。状态读取使用目录文件描述符定位文件，并检查普通文件类型、属主及链接属性。

| 状态文件或信息 | 检查内容 |
| --- | --- |
| `fides_client.mat` | 逐行解析关键词和整数计数，检查行结束、UTF-8、计数范围和重复关键词。 |
| `sse_table.mat` | 核对表名及列映射，要求与本地新版表登记一致。 |
| `row_aead_v1.key`、`locator_aead_v1.key`、`row_aead_v1.keyid` | 要求归当前用户、权限 `0600`，长度分别为 32、32、16 字节。 |
| `deployment.v1` | 检查原始 16 字节部署标识，Scope 使用其小写十六进制表示。 |
| `environment.v1` | 核对部署标识、数据库 OID、实例目录身份和密钥 ID。 |
| `row_aead_tables.v1` | 检查固定文件头及部署标识、数据库 OID、schema、表名和协议版本。 |
| 服务端目录与表 | 通过同连接读取 `public.sse_v1_catalog`，校验逻辑表版本及物理表字段。 |

`preflight` 先读取并校验本地文件，再核对传入的 Scope 和服务端目录。检查结果由 `Preflight.status`、`keyword_registered` 和 `counter` 返回。模块将健康的未登记词表示为 `ok + false`，调用方据此返回空集。损坏状态返回 `state_invalid`，密钥元信息异常返回 `key_invalid`。

命令行工具直接调用这套 C++ 检查逻辑，保留原检查器的退出码约定。

| 退出码 | 含义 | 输出方式 |
| --- | --- | --- |
| 0 | 表和关键词均已登记 | JSON 中给出计数和映射登记状态。 |
| 1 | 目标未登记 | JSON 中标记 `unregistered`。 |
| 2 | 参数错误 | stderr 输出命令用法。 |
| 3 | 状态、密钥元信息或连接校验异常 | JSON 中给出 `status` 和 `reason`。 |

启动时，`validate_local_startup` 先校验本地文件与操作标记。取得数据库连接后，`validate_startup` 继续核对实例、数据库和服务端版本登记。

### 2. 首次初始化与已有状态恢复

`initialize_state` 是显式初始化接口。它取得状态锁，确认没有已有状态且数据库新版 catalog 为空，持久化部署标识，在新建的私有空目录调用真实密钥初始化。校验三份文件和 key_id 后无覆盖发布，最后写入初始计数、映射、版本和 `environment.v1`。如果中途失败，保留现场以阻断启动和重复初始化。

已有环境通过保存的目录加载原文件。检查发现缺失、截断、密钥元信息异常或未完成标记时，返回对应错误并停止。缺失文件的用例同时断言检查结束后文件仍然缺失。

本次 `startup.cpp` 在新版状态校验之前跳过旧客户端的自动生成密钥分支。旧入口遇到带新版标识的状态目录时，提示选择显式新版模式。这样可按用户选择的协议加载对应状态。

### 3. 操作标记与契约接口

`begin_operation` 接受 `create`、`insert` 和 `search` 三种操作。持锁复核状态后，生成操作 ID，将协议版本、ID、Scope 和操作种类写入 `operation.v1`。写入并持久化成功后才返回操作句柄，供调用方继续修改。

`operation_counters` 返回持锁复核后的计数快照。调用方根据该快照计算更新值，服务端步骤全部成功后调用 `save_counters`，再调用 `complete_operation`。

`complete_operation` 检查操作 ID、本地保存是否完成以及标记内容是否仍对应本次操作，随后删除标记、同步目录并释放锁。出现保存或完成错误时，活动操作进入失败状态，后续完成调用返回 `incomplete_operation`。

中断时，标记保留在磁盘。下一次启动先检查标记，发现 `operation.v1` 就返回 `incomplete_operation`。STATE-07 用 SIGKILL 终止持锁进程，再创建新进程验证阻断结果。

### 4. 目录互斥与请求前的执行顺序

模块在同一状态目录使用固定文件 `operation.lock`，通过 `flock(LOCK_EX | LOCK_NB)` 取得独占锁。第二个进程竞争时立即返回 `busy`，避免等待期间继续使用旧计数。

操作顺序为取得锁、重新校验、持久化标记、返回操作句柄，再由调用方推进计数和发送有副作用的请求。锁或标记保存失败时，`begin_operation` 返回失败，操作句柄为空。STATE-05 和 STATE-06 检查这些分支的计数文件保持原样。

锁持续到正常完成或客户端退出。进程退出会释放系统锁，未完成标记继续承担重启后的阻断。

### 5. 计数、映射与表登记的持久化

计数通过 `save_counters` 保存，表版本登记与原表映射通过 `register_table` 保存，共用 `durable_write`。

本轮将 `Client_Sophos.cpp` 和 `sse_common.cpp` 的原有字典入口接入这套保存逻辑。新版计数推进从活动操作的快照计算，持久化成功后再更新调用方的字典和输出计数。批内 `update_file=false` 的暂存会标为待保存，完整保存后才能完成。三个原表映射入口通过 `register_current_table` 核对活动 create 及服务端结果。原 void 接口以 `std::runtime_error` 向上传播状态错误，由请求方捕获并停止。

1. 在目标文件所在目录独占创建随机临时文件。
2. 循环检查 `write` 的实际写入长度，处理短写和中断。
3. 对临时文件执行 `fsync`，检查刷新结果。
4. 关闭文件并检查 `close` 返回值。
5. 通过 `renameat` 原子替换目标文件。
6. 对父目录执行 `fsync`，确认替换结果持久化。

保存任一步失败都会返回结构化错误，活动操作保留标记并进入失败状态。STATE-09 验证映射替换失败，STATE-10 验证计数文件同步失败，STATE-11 验证标记删除和删除后的目录同步失败。

每个本地文件采用原子替换。多个本地文件与数据库请求通过操作标记关联，发生部分完成时由标记阻断后续操作，再按保留的现场进行诊断。

### 6. 新版建表登记与启动版本核对

新版建表先调用 `begin_operation(scope, "create", op)`。模块要求本地尚未登记，并核对服务端目标是否已存在。服务端建表与 catalog 登记事务成功后，调用方执行 `register_table`。

`register_table` 重新读取服务端目录和物理字段，核对活动 Scope 与协议版本，再保存 `row_aead_tables.v1` 和 `sse_table.mat`。全部保存成功后由 `complete_operation` 清标记。建表失败或映射保存失败时，标记保留，重启继续阻断。

恢复时读取器从实际连接取得数据库 OID 和实例目录，核对 `public.sse_v1_catalog` 以及 `<table>_data`、`<table>_index` 的关系类型、列名、列类型、非空属性及 rid、ut 单列主键。版本冲突返回 `state_invalid`。本次支持范围为每套状态对应一个数据库和一张新版逻辑表，关键词计数沿用现有字典。

## 四、构建与实测结果

在 WSL 外层 root 终端执行以下命令。运行配置提供当前工程的 GCC 7.3 与依赖库路径，脚本将需要验证权限的用例切换为 `omm` 执行。

~~~bash
cd /opt/og/openGauss-server
source /home/omm/.sse_opengauss_env
tools="$PWD/team_week/liu_zhuoming/LZM-02/preflight"
bash "$tools/verify-state.sh"
bash "$tools/verify-bridges.sh"
bash "$tools/verify-prerequisites.sh"
bash "$tools/verify-catalog.sh"
~~~

### 状态模块用例

| 用例 | 实际执行的检查 | 结果 |
| --- | --- | --- |
| STATE-01 | 已登记词与健康未知词返回不同结果，检查前后计数一致 | PASS |
| STATE-02 | 截断、缺失及不可读状态被拒绝，初始化未补建缺失文件 | PASS |
| STATE-03 | 密钥权限、长度、缺失及 key_id 不匹配被拒绝 | PASS |
| STATE-04 | 实例、数据库 OID、版本、物理表结构及登记异常被拒绝 | PASS |
| STATE-05 | 不可写目录、锁异常和标记保存失败发生在计数变动前 | PASS |
| STATE-06 | 第二进程竞争被拒绝，正常完成保存计数并释放锁 | PASS |
| STATE-07 | 持锁进程被终止后，新进程仍被未完成标记阻断 | PASS |
| STATE-08 | 服务端建表失败时保留标记，本地表登记为空 | PASS |
| STATE-09 | 服务端建表后本地映射替换失败，保留标记 | PASS |
| STATE-10 | 文件同步失败时原计数仍在，标记保留 | PASS |
| STATE-11 | 分别验证标记删除失败和清除后的目录同步失败 | 两个分支均 PASS |
| STATE-12 | 批内计数已暂存但尚未保存时，阻止完成并保留标记 | PASS |

完整输出保存在 [state.log](result/state.log)。

### 原保存入口接入用例

| 用例 | 实际执行的检查 | 结果 |
| --- | --- | --- |
| BRIDGE-01 | 三个原映射入口要求活动 create，服务端确认成功后保存本地登记 | 各入口均 PASS |
| BRIDGE-02 | 原字典覆盖、追加及计数推进入口拒绝无锁写入，持锁时保存活动计数 | PASS |
| BRIDGE-03 | 持锁后目录不可写，原计数保存入口抛出错误，原文件和标记保留 | PASS |
| BRIDGE-04 | 映射保存遇到异常目标类型，原映射入口抛出错误，完成操作被阻断 | PASS |

完整输出保存在 [bridges.log](result/bridges.log)。

### 隔离数据库用例

| 用例 | 实际执行的检查 | 结果 |
| --- | --- | --- |
| CATALOG-01 | 同连接执行公开物理表夹具，校验服务端版本并保存本地登记 | PASS |
| CATALOG-02 | 独立 CLI 对已登记词、未知词及参数缺失返回 0、1、2 | PASS |
| CATALOG-03 | 重编 gsql 启动后校验新版状态并到达请求接入闸门 | PASS |
| CATALOG-04 | 重编 gsql 遇到未完成标记，在用户 SQL 执行前退出 | PASS |
| CATALOG-05 | CLI 运行前后的全部新版状态文件摘要一致 | PASS |
| CATALOG-06 | 实际修改服务端 catalog 版本后，CLI 返回状态异常 | PASS |
| CATALOG-07 | 分别删除业务表 rid 主键和索引表 ut 主键，CLI 返回状态异常 | 两个分支均 PASS |
| CATALOG-08 | 分别缺少 gsql、缺少 libpq、gsql 无执行权限，验收脚本返回非零且未创建实例 | 三个分支均 PASS |

完整输出保存在 [catalog.log](result/catalog.log)。产物缺失验证输出见 [prerequisites.log](result/prerequisites.log)。联机验收要求启动检查全部执行，必要产物缺失时立即返回非零。测试生成的数据库实例已正常停止，本机原始证据目录为 `/tmp/lzm02-state.BK7RGjG3`、`/tmp/lzm02-bridges.5Z7oVGj0`、`/tmp/lzm02-catalog.Jgxeaer7`、`/tmp/lzm02-prerequisites.nv7jqajb`。本次隔离实例为 `/var/tmp/fides-isolated.qUUkjIlO`，已正常停机。前置检查前后摘要在该目录的 `evidence/` 中，公开结果随飞书材料交付。

## 五、放行条件如何完成

| 放行条件 | 实现 | 机器验证及结果 |
| --- | --- | --- |
| 重启遇到未完成标记继续阻断 | 启动先读取 `operation.v1`，存在就返回 `incomplete_operation`。 | STATE-07 用 SIGKILL 终止操作进程，新进程重新打开原目录仍被拒绝；CATALOG-04 验证 gsql 在用户 SQL 前退出。均 PASS。 |
| 缺失状态不静默初始化 | 普通检查读取已有文件，初始化只在显式空目录创建流程执行。 | STATE-02 检查缺失计数文件仍保持缺失，STATE-03 检查缺钥后仍未生成密钥。均 PASS。 |
| 检查结果可由程序判定 | C++ 返回 Status，CLI 返回 JSON 与退出码，脚本用断言比对预期。 | STATE、BRIDGE 和 CATALOG 用例均以返回状态和文件结果判断。CATALOG-08 验证缺必需产物也返回非零。PASS。 |

本轮完成 12 组状态用例、4 组保存入口用例和 8 项目录及验收检查。STATE-11、BRIDGE-01 和 CATALOG-07/08 包含多个分支，日志逐分支记录。

## 七、接手后的使用顺序

CREATE 调用顺序为 `begin_operation(create) → 服务端建表事务 → register_table → complete_operation`。

INSERT 和 SEARCH 调用顺序为 `preflight → begin_operation → operation_counters → 计数计算及服务端操作 → save_counters → complete_operation`。调用方在完成接口返回成功后再返回操作成功或展示查询结果。健康未知词在进入操作前返回空集。

本轮状态模块、保存入口和隔离目录用例已通过，现有 gsql/libpq 产物的输入指纹已记录。当前新版 gsql 在状态检查成功后返回 `unsupported`，由 ZYC 请求路由接入点继续实现业务调用。YZH 提供真实密钥初始化和加密认证，LMD 提供生产建表函数，ZYC 接入各请求分支，随后在统一版本复验完整创建、写入、搜索和重插链路。
