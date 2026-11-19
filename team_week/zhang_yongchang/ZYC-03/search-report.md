# ZYC-03：索引检索到本地明文展示

实际工具提交：`3bce830c9f428e8e3d76336e4170e5dbeb056a31`；应用基线 `c631fbc2be747e3611f00ff521ae4dd306cc2789`。契约为 SHY-02 v1.0 与 SHY-03 搜索事务/计数约定。最终结果、程序/工具字节指纹见 [verified/summary.json](evidence/verified/summary.json)。

## 链路与验证方式

沿用 `row_request_v1.cpp` 的真实客户端入口：关键词本地生成 Fides 令牌，服务端取出 `ut,locator_blob`；客户端认证恢复完整字符串 rid 并去重；重插认证位置；按 rid 定向回查；核对位置/返回集合并认证全部业务行；提交、持久化状态及清标记完成后统一展示。

本轮新增公开夹具的 libpq 边界记录及独立 `zyc-decode` 工具。真实调用发生前后保留请求/结果；派生密钥值用 `<redacted>` 代替；记录器仅在测试显式启用。正常应用构建与日志逻辑未改动。

## 本轮实库覆盖

|用例|断言|
|---|---|
|ZYC-04|apple 完整行 1/2 各一次、banana 行 3 一次；未知词正常空集且状态/数据库不变；每次使用新 gsql 进程|
|ZYC-05|追加 apple 后连续三次查询完整行 1/2/4，不丢不重；索引重插不增加业务行|
|ZYC-06|中文、空格、SQL 双单引号、引号内 AND/OR、空字段均完整恢复|
|ZYC-07|45,144 字符最大合法业务封装往返，三字段完整相等|
|ZYC-08|字段超限、含 NULL 批次、复合条件和投影非零拒绝；无业务请求/计数/物理表副作用|
|ZYC-09|第二行业务写失败导致整批回滚，保留标记，重开阻断|
|ZYC-10|真实 fetch 外加故障夹具，前两行正常、最后返回行标签篡改；认证失败非零退出、零部分明文、数据库回滚、重开阻断|
|ZYC-11|实际删除一个已知 apple 业务行并保留索引，查询报不一致；零部分明文、重开阻断；测试后恢复夹具密文|
|ZYC-12|真实 Fides 请求、加密位置、独立恢复的 rid、重插和定向 fetch 集合逐请求一致；无明文关键词谓词或原始业务 VALUES|

上游 18 项真实请求复跑另验证：健康未知词、坏状态、缺钥、异常空 token/derivation、标记写失败、额度不足以及真实重插 SQL 失败。重插失败确认数据库事务回滚、停止回查/展示、保留标记且重开阻断。18 项模块用例中的模拟发送单独计数，未替代实库请求。

## 分阶段原始证据

- [phases.json](evidence/verified/requests/phases.json)：每次成功查询的实际令牌请求、加密位置返回、独立认证后的 rid、重新加密位置的 rid、fetch 请求及返回集合、阶段事件。
- 同目录 `request-NN.wire.jsonl`：实际 libpq 请求 SQL/绑定参数、状态及加密位置/业务封装结果。业务写入、重插只允许封装参数；fetch 只按恢复出的 rid 定向调用。
- `request-NN.stdout/stderr`：真实 gsql 输出、错误退出及顺序事件；[requests.json](evidence/verified/requests/requests.json) 记录请求编号与退出码。
- [results.json](evidence/verified/requests/results.json)：13 项任务场景的断言结论；[independent-decode.json](evidence/verified/requests/independent-decode.json) 保存三条公开行与位置的独立认证结果。

这些证据来自本次实际调用和密码接口，不再用源码行存在或查询前快照替代阶段实测。记录范围是 libpq 调用边界，不作为网络抓包、传输加密或完整安全证明。

## 复跑与限制

用 [总入口](../README-ZYC-02-03.md) 在全新实例运行；只有退出 0 且最终 `ZYC_RESULT=PASS` 才视为通过。`sql/zyc03-verify-cases.sql` 是基础成功交互夹具，预期拒绝场景由自动工具分别调用并检查非零退出。

本轮已实测末行认证失败、缺业务行和重插失败；这些不再列为未执行项。跨端自动恢复、服务端重启延续、第二客户端竞争、跨表/跨库计数命名空间、密钥恢复/轮换、历史 SSE 算法故障仍未在本任务解决。正常客户端重开不等于服务端重启。

原 `evidence/live-full.log`、`live-round1.log` 为作者首次正常闭环的历史输出。当前放行结果使用 `evidence/verified/`，两类证据不混用。
