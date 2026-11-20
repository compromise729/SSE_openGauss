# YZH-02｜真实客户端故障矩阵

基线为 SHY-04 固定提交 `5c8b2b03b482e203ac65a6f989a1b669126f263c`、契约 v1。2026-11-19 在 CentOS 7 独立源码目录重新构建、安装，以全新隔离实例、公开合成数据和真实 `gsql -v ON_ERROR_STOP=1` 请求执行。SHY-04 总入口 54 项通过；本轮新增 18 项全部通过。本轮公开复验的退出码和请求编号见 [逐项结果](results/results.json)，原始 stdout、stderr 可依据 [复跑入口](handoff.md) 生成。

| 项目 | 输入和注入点 | 预期 | 实测 |
| --- | --- | --- | --- |
| N01 完整行随机化 | 两次写入完全相同的 `('同一编号','yzh02_duplicate','完整业务行 apple 空格')` | 两份 `data_blob` 与 nonce 均不同，两份完整行精确恢复 | 通过；[公开样例](results/public-duplicate-example.json) |
| N02 支持文本 | 中文、首尾空格、空字段、16/32 字节边界、长文本 | 查询结果逐字符一致 | 4 组实库用例通过；模块另测尾零字节与最大长度 |
| N03 位置载荷 | 独立认证解封当前 `locator_blob` | 恢复 rid 与操作标记；业务字段须另解密 | 通过；[公开样例](results/public-locator-example.json) |
| R01 末行标签损坏 | SHY-04 基线 ZYC-10 将多行回查的最后一行标签改坏 | 整次失败、无部分明文、退出非零 | 请求 27 退出 3，见 [尾行认证记录](results/last-row-authentication.json) |
| R02 业务封装损坏 | 密文、标签、版本、key_id、截断 | 拒绝且不打印业务行 | 5/5 通过；均退出 3；状态依次为 `auth_failed`、`auth_failed`、`protocol_invalid`、`key_invalid`、`protocol_invalid` |
| R03 跨记录业务替换 | 保留 rid，替入另一记录的合法 `data_blob` | rid 绑定认证失败 | 通过；退出 3、`auth_failed` |
| L01 位置封装损坏 | 密文、标签、版本、key_id、截断 | 认证或格式失败，不解析坏位置 | 5/5 通过；均退出 3；状态依次为 `auth_failed`、`auth_failed`、`protocol_invalid`、`key_invalid`、`protocol_invalid` |
| L02 跨表位置替换 | 同一测试密钥、另一表作用域生成合法位置封装，替入当前索引 | 表作用域认证失败 | 通过；退出 3、`auth_failed` |
| K01 密钥故障 | 错误行密钥、缺失行密钥、8 字节短密钥 | 不换钥、不输出业务明文，非零退出 | 3/3 通过；错钥退出 3、`auth_failed`；缺钥与短钥退出 1、`key_invalid` |

物理表的封装格式 CHECK 会先拦截坏版本和截断封装。对应测试在隔离实例中暂时移除该约束，写入坏封装后从真实客户端查询，最后恢复原密文与约束。所有失败查询均核对 stdout 无业务行、查询前后物理表快照一致。测试文件为 `yzh02_cases.py`、`yzh02_session.sh`、`mint_foreign_locator.cpp`。

本轮没有发现需要修改密码模块或调用方的失败项。最初两轮夹具因 gsql 启动提示混入 stdout、以及物理 CHECK 拦截注入而失败；修正夹具后完整复跑，作者原始正式结果随本地交付原件保留，本次组长复跑见 [复验摘要](results/review.json)。
