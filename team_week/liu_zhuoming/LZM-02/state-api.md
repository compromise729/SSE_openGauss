# LZM-02｜Fides v1 状态接口

接口冻结于 SHY-02 v1.0。公开类型和函数见 `src/include/libpq/sse_v1_contract.hpp`，内部启动、连接和保存接口见 `row_state_v1.h`。本轮每套状态只支持一个实例中的一个数据库和一张 `public` 下的新版逻辑表。计数仍以关键词为索引。

| 接口 | 调用条件与结果 |
| --- | --- |
| `set_catalog_reader`、`read_catalog` | 用实际 Unix socket 的同一个连接读取实例目录、数据库 OID、catalog 和物理表。连接有变化时，下一次操作必须使用对应的读取器和 Scope。测试使用显式夹具适配器。 |
| `initialize_state(database_oid, initialize_keys)` | 只在调用方显式创建隔离环境时调用。持锁确认没有已有状态且数据库新版 catalog 为空，先持久化部署标识。将新建的私有空目录传给真实 YZH-01 密钥初始化函数，校验三份文件和 key_id 后无覆盖发布，最后写 `environment.v1`。部分初始化的现场会阻断启动和再次初始化。 |
| `validate_startup()` | 核对未完成标记、状态文件、密钥元信息、本地登记和同连接目录。在新版 gsql 启动时先做此检查，然后才允许路由接入。 |
| `preflight(scope, keyword)` | 只读。健康未登记词返回 `ok`、`keyword_registered=false`，查询方可直接返回空集。格式、版本或环境异常为 `state_invalid`，密钥元信息异常为 `key_invalid`，已有标记为 `incomplete_operation`。 |
| `begin_operation(scope, kind, op)` | `kind` 为 `create`、`insert` 或 `search`。先尝试独占目录锁，再重新检查状态，持久化标记后返回操作 ID。同目录的第二个写操作返回 `busy`。任何失败均不发业务请求、不推进计数。 |
| `operation_counters(op, out)` | 取得持锁重读后的计数快照。用该快照推进计数，避免沿用另一个客户端完成写入前的内存副本。 |
| `register_table(scope, version)` | 只接受对应的活动 `create`。服务端建表和目录登记已经提交后调用。再次读取服务端目录和表形状，保存本地版本登记及原表映射。 |
| `save_counters(op, counters)` | 对活动 `insert` 或 `search` 保存完整计数，调用方必须检查返回值。失败后本进程保持锁和标记并停止。 |
| `complete_operation(op)` | 调用方确认所有服务端步骤成功后调用。模块验证操作 ID 和本地持久化结果，清除并同步标记后释放锁。完成失败时禁止继续发送请求或展示准备好的结果。 |
| `load_table_version(scope, version)` | 校验本地和服务端登记并返回新版协议版本。无登记或校验失败时版本输出为 0。 |

## 文件及失败语义

- `deployment.v1` 是原始 16 字节部署标识。`environment.v1` 绑定部署标识、实例目录、数据库 OID 和密钥 ID。密钥检查要求三份预期文件归当前用户、为普通文件、权限 `0600`，长度分别为 32、32、16 字节，并核对已保存的 ID。真实密文认证及密钥装载由密码模块负责。
- `fides_client.mat` 和 `sse_table.mat` 保留原行格式。`row_aead_tables.v1` 采用契约头和 TSV。新版状态解析拒绝重复、截断及映射冲突。启动只读取时不会创建锁或补状态。
- `operation.lock` 是固定文件，留在原目录中。`operation.v1` 记录操作种类、ID 和 Scope，存在即阻断启动。异常后操作调用方应终止本次客户端，系统会关闭锁描述符。保留标记与整个状态目录用于人工诊断。
- 写入使用同目录临时文件，检查全部 `write`、`fsync`、`close`、`rename` 和目录 `fsync`。文件内容按单文件原子替换。计数、映射、服务端结果之间没有跨端事务。若文件系统在清标记的目录同步阶段报错，模块尝试恢复标记并报告结果。同步和恢复均失败时不能承诺断电后的目录状态，应隔离现场再核对服务端，不能盲目重试。

## 原有保存入口的新版接入

显式选择新版状态后，`Client_Sophos.cpp` 的四个计数推进重载，以及该文件和 `sse_common.cpp` 的字典写入、追加入口，统一调用状态模块。`increment_counter` 从活动操作的持锁快照推进计数。`persist=false` 用于批内暂存并标记本地状态待保存，完整计数持久化之后才能完成操作。

`SSE_map.cpp` 的 `appendTableMap`、`writeTableMap` 和 `table_map_write` 在新版模式下统一调用 `register_current_table`。它要求活动 `create`、对应表名和固定三列映射，核对服务端结果后保存两份本地登记。

原有 void 接口通过 `std::runtime_error` 向请求调用方传播状态错误，消息含结构化状态名。ZYC 在调用边界捕获该异常，停止本次请求、保留标记并返回失败。调用前必须已成功执行 `begin_operation`。缺少活动操作时返回或抛出 `incomplete_operation`，推进函数的输出计数清为 0，调用方的计数字典保持原样。

服务端结构校验同时检查 `rid` 和 `ut` 的单列主键。缺少主键或键列与协议不符时，返回 `state_invalid`。

## 请求方接入顺序

CREATE 先 `begin_operation(create)`，再执行服务端事务，之后 `register_table`、`complete_operation`。INSERT 和 SEARCH 先做只读检查，进入有副作用的步骤前调用 `begin_operation`，使用 `operation_counters` 取得持锁计数。全部服务端步骤成功后调用 `save_counters` 和 `complete_operation`，最后才返回成功或展示结果。健康的未知词在进入操作前直接返回空集。任何请求、校验或持久化失败时都停止，不调用成功完成接口。用户外层事务里的新版写操作由请求入口拒绝。
