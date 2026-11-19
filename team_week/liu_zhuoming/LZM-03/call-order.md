# LZM-03｜真实请求调用顺序与标记处理

本文事件表保留 `0671a79` 的原始记录。当前版本在搜索认证、回查成功后，先提交数据库事务，再保存本地计数和清除标记。计数额度不足时按是否已经开启数据库事务处理，当前版本的完整顺序见 [契约修复与原生复验](契约修复与原生复验.md)。

本轮从基线 `eae777379d0aa81f8d9467fce6a44298e595215a` 接续请求源码修复，完整运行退出 0，15 个真实请求用例通过。

## 一、实际入口和顺序

`src/bin/psql/common.cpp` 的 SendQuery 在旧 SQL 改写之前调用 execute_request。`startup.cpp` 保留本地与同连接目录检查，通过后允许进入请求入口。`row_request_v1.cpp` 实现新版 CREATE/INSERT/SELECT。

CREATE：解析与密钥检查 → begin_operation(create) → 服务端建表/登记 → 本地版本与映射保存 → complete_operation → 返回。

INSERT：解析/长度与真实加密检查 → preflight → begin_operation(insert) → 从持锁快照构造 Fides 客户端 → 生成更新令牌并推进内存计数 → 整批数据库写入 → 持久化计数 → complete_operation → 返回。

SELECT：密钥/版本检查 → preflight → 未登记词短路 → begin_operation(search) → 从持锁快照构造客户端 → 生成并校验令牌 → fides_setup → 真实 Fides 索引取出 → 核对返回 UT 属于本次请求 → 认证位置并去重 rid → 生成新令牌/重插 → 按 rid 回查 → 全部业务行认证与集合核对 → 状态保存 → complete_operation → 展示。

Fides 客户端使用 current_operation_counters 取得 begin 持锁重读的完整字典，支持含空格的关键词。计数推进和索引取出均在 begin 持久化成功之后；本地状态失败时不会调用成功完成。

## 二、实测事件顺序

| 用例 | 真实事件顺序和结果 |
| --- | --- |
| LIVE-02 | begin_durable → counter_advanced → insert_sent → state_saved → complete_durable。apple 计数为 2。 |
| LIVE-03/04 | preflight → begin_durable → token_generated → search_take_sent → counter_advanced → reinsert_sent → fetch_sent → rows_authenticated → state_saved → complete_durable。apple 计数 2→4→6，每次返回编号 1、2 各一次。 |
| LIVE-05 | keys_loaded → preflight → unknown_empty；未建标记，无业务发送，状态/数据库摘要不变。 |
| LIVE-06/07 | 标记 rename 失败，未出现 begin_durable、counter_advanced 或业务发送事件；客户端非零，状态/数据库摘要不变。 |
| LIVE-12/13 | begin_durable → token_generated 后返回 protocol_invalid；没有搜索取出或计数推进，保留标记，新进程 incomplete_operation。 |
| LIVE-15 | begin_durable → search_take_sent → counter_advanced → reinsert_sent 后 SQL 错误；没有 fetch_sent 和 complete_durable，不展示，重开阻断。 |

完整事件见 [live-cases.json](result/live-cases.json)，对应真实请求 stdout/stderr 见 [live/](result/live/)。静态核对文件保留 REVIEW_REQUIRED 的含义是“不能仅凭源码存在调用判断顺序”；总体结论结合实际事件和数据摘要断言，见 [release.json](result/release.json)。

## 三、各成功/失败分支的标记处理

| 分支 | 当前实现的处理 | 验证依据 |
| --- | --- | --- |
| 新环境初始化成功 | 持父目录锁，私有空目录生成三文件，校验发布后最后保存 environment.v1。 | KEY-01 |
| 初始化部分失败 | 保留现场；启动和重复初始化拒绝，不生成完整环境。 | KEY-08 |
| 输入/长度/版本不支持 | begin 前返回 invalid_input/unsupported，不推进计数。 | 源码分支核对；本轮未逐项重测所有语法边界。 |
| 已有数据缺钥、短钥、错误 key_id | 拒绝加载和发送，不补钥；已有标记不清除。 | KEY-03～07；LIVE-09～11 |
| 健康未知词 | 返回正常空集，不调用 begin，不建标记。 | FLOW-01、LIVE-05 |
| 状态损坏或已有未完成操作 | 启动/preflight 返回错误，不发业务请求，保留现场。 | FLOW-02、LIVE-08、LIVE-12/13/15 的重开 |
| 锁竞争失败 | 返回 busy，不清除其他进程的标记。 | 沿用 LZM-02 STATE-06；本轮未重复实际客户端竞争。 |
| begin 标记保存失败 | 不推进计数、不取出索引、不调用 complete；保留可能已落盘的标记和诊断。rename 注入时无新标记。 | ORDER-01/02、HOOK-01、LIVE-06/07 |
| 已登记词异常空令牌 | protocol_invalid，禁止发送；已创建的标记保留。 | FLOW-03/04、LIVE-12/13 |
| Fides 基础密钥/客户端对象加载失败 | key_invalid，停止；若 begin 已成功则保留标记。初始化仅显式入口执行。 | 源码分支核对 |
| SQL 插入/搜索取出失败 | db_failed，停止后续操作，保留标记。 | 源码分支核对；本轮真实 SQL 错误注入覆盖重插。 |
| 返回 UT、位置认证或操作类型异常 | protocol_invalid/auth_failed/unsupported，停止重插与回查，保留标记。 | 源码核对；正常 UT 匹配和认证由真实查询覆盖。 |
| 重插失败 | 检查 PGresult 后立即停止，不回查、不展示，保留标记。 | LIVE-15，真实数据库触发器注入 |
| 回查集合或业务行认证失败 | inconsistent_result/auth_failed，清除未交付输出，保留标记。 | 源码核对；正常集合与认证见 LIVE-03/04/14。 |
| 计数/映射保存失败 | 停止，保留标记，不执行成功完成。 | 状态模块原 STATE-09～12；本轮正常保存见真实请求。 |
| complete 清除/同步失败 | 返回错误，不展示；按状态模块既有逻辑保留或恢复标记。 | 状态模块原 STATE-11；本轮未对真实客户端再次注入清除故障。 |
| 全部成功 | complete 持久化清标记后才把结果交给 gsql 展示。 | LIVE-01～04、14 |
| 异常进程退出/重开 | 系统释放锁，标记仍阻断新进程。 | LIVE-12/13/15；强制退出机制沿用 STATE-07。 |

## 四、交接确认

本次按用户要求补齐请求接入，并用实际 gsql、Fides 和数据库执行事件确认 begin 的顺序。张永昌和组长可据此复验，签收状态在飞书填写；本轮未代发消息。组长公布后续输入版本后，以新版本完整复跑，不将本轮结果套用到后续修改。
