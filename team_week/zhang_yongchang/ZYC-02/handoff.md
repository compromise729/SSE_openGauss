# ZYC-02 交接记录

|项|本轮交付|
|---|---|
|前置|SHY-03：`8111044852770529bfacde4de5af5690dd6a8322` 已包含于应用基线 `c631fbc2be747e3611f00ff521ae4dd306cc2789`|
|被测工具提交|`a0f563fe586cca1c72384b40397dc614954e0080`；完整程序/工具指纹见 `../ZYC-03/evidence/verified/summary.json`|
|协议|SHY-02 v1.0，SHY-03 请求、额度、搜索事务约定|
|源码归属|复用已发布客户端/密码/状态/服务端实现；本轮只修复验收工具、证据和说明|
|修复|LF、源码定位、辅助程序自动编译、失败非零退出、精确结果及阶段断言|
|实际验证|3 入口回归、18 模块、18 上游真实请求、13 ZYC 实库场景；详见报告和 verified 证据|
|写入要点|三条真实入库并由独立进程解密；登记失败无物理残留；第二行错误整批回滚；长度/输入拒绝无副作用|
|构建|同批原生 `make -j6` 与 `make install`；本轮为已配置独立副本的增量构建|
|复跑|外层 root：`bash "$repo/team_week/zhang_yongchang/tools/zyc_live_verify_all.sh" --project-root "$repo"`|
|版本|[PR #14](https://github.com/compromise729/SSE_openGauss/pull/14)，`feature/ZYC-02-encrypted-write` 同批交付 ZYC-02/03；文档提交可晚于被测工具提交|
|限制|单客户端单新版逻辑表；无自动跨端恢复、密钥恢复/轮换、服务端重启或多表并发验收|
|接续|组长复核并发布 SHY-04 输入；张永昌接续查询/联合问题，三名模块负责人支持|

接收人核对 summary 的提交和工具 SHA、运行总入口、核对新证据后填写团队签收；本次 Git 修复未代写飞书签名。历史作者交付仍保留于 Git 历史和原 evidence 文件。
