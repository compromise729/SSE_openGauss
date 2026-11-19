# ZYC-03 交接记录

|项|本轮交付|
|---|---|
|前置|同批 ZYC-02 写入复验；应用基线 `c631fbc2be747e3611f00ff521ae4dd306cc2789`|
|被测工具提交|`3bce830c9f428e8e3d76336e4170e5dbeb056a31`；完整程序/工具指纹见 `evidence/verified/summary.json`|
|协议|SHY-02 v1.0 / SHY-03 搜索事务与计数约定|
|源码归属|沿用现有真实 Fides 链路和新版字符串 rid 过滤；本轮新增验证工具，不覆盖其他负责人应用模块|
|正常结果|apple 两条、banana 一条、未知词零条；重复查询、追加、新客户端重开、中文/空格/空串、SQL 转义均精确比较|
|失败结果|末行认证失败、真实缺行、真实重插失败均非零，无部分明文，保留标记且重开阻断；输入拒绝无副作用|
|阶段证据|`evidence/verified/requests/phases.json`、原始 wire.jsonl、stdout/stderr、requests.json、results.json|
|独立解密|独立进程认证实际位置及业务封装；恢复 rid 与重插/fetch 集合一致|
|复跑|按总入口自动建新隔离实例、显式初始化、运行完整验收并停止；只认最终 PASS 和退出 0|
|版本|[PR #14](https://github.com/compromise729/SSE_openGauss/pull/14)，`feature/ZYC-02-encrypted-write`；以被测工具 SHA 匹配后续文档提交|
|未完成|自动跨端恢复、服务端重启延续、第二客户端竞争、多库多表隔离、密钥恢复/轮换、历史算法故障；不以本轮宣称解决|
|接手|组长发布统一 SHY-04 输入；严子涵、李孟德、刘卓明支持各自模块联测|

末行篡改与缺行业务失败已经实际执行，不再引用旧报告的“本轮未注入”。原 live 日志为历史样例，新结论来自 verified。团队签收仍由实际接收人填写。
