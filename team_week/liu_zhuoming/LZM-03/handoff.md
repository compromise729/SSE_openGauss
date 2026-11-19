# LZM-03｜真实密钥、请求顺序与失败阻断联测报告

日期：2026-11-19。负责人：刘卓明。接手人：组长、张永昌。

本文记录原始交付 `0671a79`。当时的临时构建通过 18 项模块用例和 15 项真实请求用例。PR #11 审查后的契约修复与当前证据见 [契约修复与原生复验](契约修复与原生复验.md)。

## 一、任务要求与完成内容

| 要求 | 实现与验证 |
| --- | --- |
| 读取 YZH-01 的 key_id/路径约定，验证真实初始化、重复加载及已有数据缺钥拒绝 | 真实初始化、32/32/16 字节三文件、ID 对应、行与位置认证加解密、同密钥新进程重开均通过。真实数据库已有数据缺三类密钥分别拒绝，不生成新钥。 |
| begin 必须早于计数更新和搜索取出索引，书面列出标记处理 | 已补齐真实请求入口，实际事件证明 begin_durable 先于 counter_advanced/search_take_sent；标记写失败时两者均未发生，状态和数据库摘要不变。分支表见 call-order.md。 |
| 未登记、坏状态和异常空令牌不能混为一类，提供注入步骤 | 实际 gsql 分别返回正常零行、state_invalid、protocol_invalid；异常保留标记并在新进程阻断。故障库、工具和手动命令已交付。 |

## 二、被测版本与交接边界

固定基线：`eae777379d0aa81f8d9467fce6a44298e595215a`，来自 integration/SHY-03，由用户确认。本轮基线之上补齐了请求接入与持锁计数快照，**被测对象是该基线加本轮源码修复，不是未经修改的 eae777 提交**。

契约仍为 SHY-02 v1.0，公开签名保持。源码与工具的 SHA-256、临时产物指纹和实测位置见 [input.json](result/input.json)。本轮源码与工具已由用户提交到 `test/LZM-03-state-crypto`，输出提交号为 `0671a79d068b8c3535b7c511c4aeefe76710a032`。本地核对确认仅包含8个源码文件和10个工具，提交内容与实测版本一致，记录见 [commit-check.log](result/commit-check.log)。原始测试 JSON 保留运行时的版本描述，最终输出提交记录在 input.json。

## 三、源码修改清单与原因，共 8 个文件

链接固定到原交付提交 `0671a79`，便于同原始验证日志核对。

| 文件 | 修改原因与实际行为 |
| --- | --- |
| [src/bin/psql/common.cpp](https://github.com/compromise729/SSE_openGauss/blob/0671a79d068b8c3535b7c511c4aeefe76710a032/src/bin/psql/common.cpp) | SendQuery 在旧改写前调用新版请求模块，失败返回非零；成功完成后才交给原 gsql 结果展示。旧模式保留原路径。 |
| [src/bin/psql/startup.cpp](https://github.com/compromise729/SSE_openGauss/blob/0671a79d068b8c3535b7c511c4aeefe76710a032/src/bin/psql/startup.cpp) | 保留已有状态/密钥/服务端版本检查，检查成功后允许进入请求路由，移除原 unsupported 闸门。 |
| [src/common/interfaces/libpq/Client_Sophos.cpp](https://github.com/compromise729/SSE_openGauss/blob/0671a79d068b8c3535b7c511c4aeefe76710a032/src/common/interfaces/libpq/Client_Sophos.cpp) | 新版 Fides 对象从活动操作的持锁计数快照构造，避免读错旧文件或错误拆分带空格关键词；不自动生成旧钥。 |
| [src/common/interfaces/libpq/row_state_v1.cpp](https://github.com/compromise729/SSE_openGauss/blob/0671a79d068b8c3535b7c511c4aeefe76710a032/src/common/interfaces/libpq/row_state_v1.cpp) | 增加 current_operation_counters，只在活动 insert/search 持锁期间返回最新计数。 |
| [src/include/libpq/row_state_v1.h](https://github.com/compromise729/SSE_openGauss/blob/0671a79d068b8c3535b7c511c4aeefe76710a032/src/include/libpq/row_state_v1.h) | 声明内部持锁计数读取接口，不改变公开契约签名。 |
| [src/common/interfaces/libpq/Makefile](https://github.com/compromise729/SSE_openGauss/blob/0671a79d068b8c3535b7c511c4aeefe76710a032/src/common/interfaces/libpq/Makefile) | 纳入请求模块，并以同一前端 ABI 构建真实 YZH 密码实现。 |
| [src/common/interfaces/libpq/row_request_v1.cpp](https://github.com/compromise729/SSE_openGauss/blob/0671a79d068b8c3535b7c511c4aeefe76710a032/src/common/interfaces/libpq/row_request_v1.cpp) | 实现受限新版 CREATE/INSERT/SELECT；预检、begin、真实 Fides、认证、重插/回查、状态保存、complete；统一错误和输出。 |
| [src/include/libpq/row_request_v1.h](https://github.com/compromise729/SSE_openGauss/blob/0671a79d068b8c3535b7c511c4aeefe76710a032/src/include/libpq/row_request_v1.h) | 声明 gsql 请求入口，明确 PGresult 只在操作完整成功后交付。 |

请求模块检查返回 UT 属于本次真实 Fides 派生集合，认证位置载荷并按 rid 去重，回查集合准确匹配，全部业务行认证后才允许展示。内部查询使用参数化 SQL，外层事务中的新版请求被拒绝。单库、单新版逻辑表和受限 SQL 范围保持。

## 四、10 个可复跑工具及文档作用

| 工具，位于 preflight/ | 作用 |
| --- | --- |
| [verify-all.sh](preflight/verify-all.sh) | 完整入口，组合模块、临时构建和真实联调，全部断言通过才输出 RELEASE_RESULT=PASS。 |
| [verify.sh](preflight/verify.sh) | 18 个模块用例与静态源码核对，独立运行不单独宣布整体放行。 |
| [state-crypto-cases.cpp](preflight/state-crypto-cases.cpp) | 真实状态/密码模块夹具、跨进程重开和故障顺序断言。 |
| [audit-call-order.py](preflight/audit-call-order.py) | 核对 gsql 路由与请求模块接口调用，保存行号片段。静态检查与真实事件分开。 |
| [build-live.py](preflight/build-live.py) | 复用工程现有编译参数和对象，在临时目录构建新客户端与服务端。 |
| [initialize-live.cpp](preflight/initialize-live.cpp) | 在真实隔离连接上显式初始化空的新版状态及真实密钥。 |
| [live-cases.py](preflight/live-cases.py) | 自动执行 15 个真实请求用例，比对实际计数、数据库摘要、输出及事件。 |
| [run-live.sh](preflight/run-live.sh) | 私有命名空间运行临时服务端，启动/停止本次实例，比较环境内外摘要。 |
| [fault-hook.cpp](preflight/fault-hook.cpp) | 仅对指定个人状态目录的 operation.v1 rename 注入 EIO。 |
| [live-fault-hook.cpp](preflight/live-fault-hook.cpp) | 实际 Fides 令牌生成后注入空字段，并记录无密钥内容的请求事件。 |

原始联测使用临时产物。后续审查发现和原生复验见 [契约修复与原生复验](契约修复与原生复验.md)，后续事项见 [后续修改建议](后续修改建议.md)。

文档为 [README.md](README.md)、[env.md](env.md)、本文、[call-order.md](call-order.md)、[fault-injection.md](fault-injection.md)。结果在 result/，原接入缺项证据保留于 result/before-integration/。

## 五、密钥约定和真实初始化

YZH 文件约定为 row_aead_v1.key=32 字节、locator_aead_v1.key=32 字节、row_aead_v1.keyid=原始 16 字节；目录归当前用户、0700，文件普通且0600。ID 文件不是十六进制文本。状态从 SSE_V1_STATE_DIR 定位，密码函数使用显式目录参数。

首次初始化持状态互斥锁，在私有空子目录调用真实 YZH 初始化，校验三份文件和 key_id 后发布，最后写 environment.v1。模块用例在初始化回调中用第二进程确认父锁确实被持有。部分失败时拒绝启动和重复初始化。

实际联调中，新版状态位于个人目录 evidence/v1-state；既有 Fides 基础密钥在同一次隔离的 client 目录，实际 Fides 客户端只加载已有文件。原安装程序在新隔离实例准备阶段显式建立这套基础环境，已有数据加载路径不生成密钥。

## 六、用例及输出

### 模块用例 18 项

| 用例 | 实测结果 |
| --- | --- |
| KEY-01/02 | 真实初始化、行和位置认证加解密、重复加载、新进程重开，字节/权限不变；PASS。 |
| KEY-03～07 | 缺三类密钥、短钥、错 ID 拒绝；既有样本/计数保留，输出清空；PASS。 |
| KEY-08 | 真实初始化中途失败，重开及再次初始化拒绝；PASS。 |
| FLOW-01～04 | 正常未知词、坏状态和异常空令牌分类不同；PASS。 |
| FLOW-05 | 模拟重插失败，不展示且重开阻断；PASS。 |
| ORDER-01/02 | 实际标记保存失败先于夹具副作用；PASS。 |
| ORDER-03/04 | 顺序夹具成功，完成后清标记；PASS。 |
| HOOK-01 | 实际 preload 注入标记失败，提前阻断；PASS。 |

模块数据库/网络仍为显式夹具；实际数据库由以下用例验证。单项 JSON 见 [cases/](result/cases/)，原始模块结果见 [module-results.json](result/module-results.json)。

### 真实请求用例 15 项

| 用例 | 输入、预期与实际 |
| --- | --- |
| LIVE-01 | 通过实际 gsql 新建 week_demo；服务端表与本地登记保存，标记清除；PASS。 |
| LIVE-02 | 插入两个 apple 和一个 banana，真实 AEAD 行/位置载荷；apple 计数2，业务表/索引各3行；PASS。 |
| LIVE-03 | 查询 apple，返回编号1、2各一次；计数2→4，业务与索引仍3行；PASS。 |
| LIVE-04 | 新进程重新加载同密钥、同状态再次查询；编号仍各一次，计数4→6；PASS。 |
| LIVE-05 | unknown 正常零行，不建标记；全部状态与数据库摘要不变；PASS。 |
| LIVE-06 | INSERT 标记替换失败，非零、计数和数据库摘要不变，无插入事件；PASS。 |
| LIVE-07 | SELECT 标记替换失败，非零，无索引取出事件，状态/数据库摘要不变；PASS。 |
| LIVE-08 | 已有状态计数截断，state_invalid，无业务发送；PASS。 |
| LIVE-09～11 | 真实数据库已有数据，分别缺行密钥、位置密钥、ID；key_invalid，无补钥/状态变化；PASS。 |
| LIVE-12/13 | 已登记词实际生成令牌后注入空 token/derivation_key；protocol_invalid，无取出；标记保留，新进程阻断；PASS。 |
| LIVE-14 | 插入关键词 red AND green 和载荷 it's 中文（SQL 中转义为 it''s 中文）；新进程两次正常查询，关键词计数1→2→3；PASS。 |
| LIVE-15 | 真实 SQL 触发器使重插失败；停止回查/展示，保留标记，新进程阻断；业务4行、索引剩2行；PASS。 |

结果与逐步事件见 [live-cases.json](result/live-cases.json)、[live/](result/live/)。既有 ZYC-01 的16个规则模型已在前轮通过，日志继续保留于 zyc-model.log；本轮放行主要依据实际请求。

最终输出：

~~~text
LIVE_RESULT=PASS
ISOLATED_SERVER_STOPPED
LIVE_EXIT=0
RELEASE_RESULT=PASS
MODULE_CASES=18 REAL_REQUEST_CASES=15
~~~

总体机器结果见 [results.json](result/results.json) 和 [release.json](result/release.json)，完整运行日志为 [verify.log](result/verify.log)。

## 七、放行条件与环境证据

| 放行条件 | 本轮依据 |
| --- | --- |
| 新密钥与状态模块兼容 | 真实初始化/认证加解密/新进程加载通过，已有数据库缺钥明确拒绝，不重建。 |
| 不能先改状态再发现标记失败 | LIVE-06/07 实际注入 operation.v1 rename 失败，真实计数、映射和整个数据库摘要一致，事件中无计数推进和业务发送。 |

本轮使用临时构建程序，不覆盖任何已安装程序或库。服务端仅在子挂载命名空间中绑定到本次实例使用的程序路径。安装程序/库、原客户端/服务端目录的摘要前后一致，证据见 [environment.log](result/environment.log)。

实测目录：总证据 `/var/tmp/state-release-check.AjvXiziG`；模块证据 `/var/tmp/state-crypto-test.EfWf2Wax`；真实运行证据 `/var/tmp/state-live-evidence.6dVdG2eV`；个人实例 `/var/tmp/fides-isolated.KvNT98Su`，已正常停止。真实密钥、密钥摘要和完整故障现场留在本机私有目录，不放入公开交付。

## 九、保留范围

单实例、单库、单新版逻辑表限制保持；不完成跨表计数作用域重构。单文件原子替换不等于跨端事务。真实重插失败后的索引缺口已观察并阻断，没有自动补索引、清标记、换钥或安全重试流程。后续源码更新后，以组长公布的新提交复验；服务重启恢复与更多中断故障属于后续任务。
