# ZYC-02：用户 SQL 到密文写入

本轮复验结果和输入指纹见 [verified/summary.json](../ZYC-03/evidence/verified/summary.json)，用例见 [results.json](../ZYC-03/evidence/verified/requests/results.json)。实际工具提交：`a0f563fe586cca1c72384b40397dc614954e0080`。应用源自 `c631fbc2be747e3611f00ff521ae4dd306cc2789`，SHY-02 v1.0 字节布局及 SHY-03 请求/计数/事务约定不变。

## 实现与本轮改动

`row_request_v1.cpp` 已实现三字段 SQL 解析、随机 rid、整行业务认证加密、位置认证加密、状态持锁重检与额度、操作标记、参数化 `sse_v1_insert`、状态保存及完成标记；`common.cpp` 将新版请求接入展示/退出状态。复用该实现，不再修改应用源码或重新引入旧 join_col 路径。

本轮将验收脚本改为 LF、当前源码定位、自动构建辅助程序、明确依赖检查、失败非零退出及严格结果断言，替换旧的仅打印 rc 和 grep 泄露扫描；新增独立解密与真实请求记录。

## 任务覆盖与证据

|任务要求|本轮核验与预期|证据|
|---|---|---|
|固定 SHY-03 输入和接口|统一源码原生 make/install，记录提交及同批程序摘要|summary.json、build-evidence.txt|
|新表创建与登记|创建物理 data/index 和 catalog；新客户端进程可识别|ZYC-01、ZYC-04|
|登记失败原子性|人为拒绝 catalog 登记，无两张残留物理表，无 catalog 记录；保留失败标记，重开阻断|ZYC-00|
|三条真实写入|apple 两条、banana 一条，物理表仅 rid/业务封装及 ut/位置封装|ZYC-02、ZYC-03|
|独立客户端解密|独立 `zyc-decode` 进程加载私有测试密钥，认证解密实际三条业务行和全部位置，逐字段匹配输入|independent-decode.json|
|发送前加密|实际 `sse_v1_insert` 参数为 rid、业务/位置封装和 ut；核对封装用途和数组数量，无原始 VALUES|ZYC-12、每请求 wire.jsonl|
|多行失败反馈|第二条数据行触发 SQL 错误，第一条也回滚；计数/业务表无假成功，保留标记，重开阻断|ZYC-09|
|长度、转义与拒绝|中文/空格、双单引号、引号内 AND/OR、合法空字段；三字段最大值完整往返；超限及含 NULL 批次在副作用前拒绝|ZYC-06/07/08|
|密钥/状态前置检查|缺钥、短钥/错误 key_id、坏状态、标记写失败和额度等|18 项模块与 18 项真实客户端结果，分别计数|

最大业务行字段字节数为 `16384/1024/16384`，合法编码 33,804 字节，Base64 业务封装 45,144 字符；本轮逐字节比较完整恢复值，无截断。

## 复跑

按 [总入口](../README-ZYC-02-03.md) 在外层 root 运行 `tools/zyc_live_verify_all.sh --project-root "$repo"`。不需要预先在作者个人目录放置 `initialize-live`。同批构建程序及 SQL 的摘要由入口写入 summary。

`sql/zyc02-verify-cases.sql` 仅供已完成显式初始化的新隔离实例手工演示。完整自动验收使用入口中逐请求断言，不把预期失败 SQL 混进成功批处理。

## 边界与历史证据

本轮限定单客户端、单新版逻辑表的可丢弃环境。失败标记阻断并不提供跨端自动恢复、丢钥恢复或轮换；不证明服务端重启恢复、多表并发隔离、旧表迁移、TLS、性能或历史算法全面修复。

本目录原有 `evidence/build-*` 和 `write-session.log` 为作者首次交付记录，保留其原文；其中 `.git` 基线加源码覆盖的构建方式与旧路径仅作历史追溯。本轮固定 Git 检出和实际 SHA-256 见 verified 证据，不将原始历史日志充作本轮新实测。
