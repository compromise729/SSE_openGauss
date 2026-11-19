# ZYC-03 交接记录：完成索引检索到本地明文展示

|项|内容|
|---|---|
|前置任务编号|**ZYC-02**（本批同提交同会话验收 PASS）|
|实际输入提交号|`c631fbc2be74…`（main，含 SHY-03 发布 `8111044`）；检索链路代码在 `row_request_v1.cpp` 的 `execute_request()` SELECT 分支|
|合同版本|SHY-02 v1.0；SHY-03 搜索事务约定（取出索引→认证→重插→回查→提交→清标记→展示）|
|本人输出提交号/补丁|无独立源码提交（检索实现已在输入提交，由 SHY-03/LZM-03 落地并经我逐项核对）；本目录交付检索验收报告 + 完整原始日志。**Git 交付分支 `feature/ZYC-02-encrypted-write`，输出提交 `3a22a5311c6af8b7aad77958f8516588667ca451`，PR [#14](https://github.com/compromise729/SSE_openGauss/pull/14)**（提交内容为验收材料与可复跑脚本，不含源码改写）|
|改动文件及原因|本轮不改源码。`Client_Sophos.cpp` 状态读写/过滤归刘卓明、`startup.cpp` 归刘卓明、密码模块归严子涵、服务端 SQL 归李孟德，均未触碰；新版字符串 rid 去重在 `row_request_v1.cpp` 内已由输入提交实现|
|构建/运行命令|同 ZYC-02 的原生构建产物；隔离实例 `LZM-01/isolated.sh init --port 55439`，会话内 `fides_setup` → `SSE_v1.sql` → `initialize-live` → CREATE/INSERT/SELECT|
|测试输入与预期/实际|apple 2 行、banana 1 行、未知词 0 行、重复 apple 仍 2 行、中文/空格 2 行、SQL 转义 1 行、AND/投影拒绝 rc=3、无明文泄露——全部 PASS，详见 `search-report.md` 第 3 节与 `evidence/live-full.log`|
|原始日志路径|`evidence/live-full.log`（210 行，含两轮全部请求 stdout/stderr、物理表密文、各步 rc）；`evidence/live-round1.log`（首轮基础三行）|
|未完成项|重插失败后跨端恢复、最后一行篡改/缺行业务行故障注入、服务重启延续、第二客户端竞争——见 `search-report.md` 第 5 节；相关结论引用 SHY-03 证据，未记为本人新增实测|
|接手人|组长接手 SHY-04；严子涵、李孟德、刘卓明支持各自模块问题|
|复现命令|见 `search-report.md` 第 6 节；新建隔离会话后按 ZYC-02 顺序建表写入再检索|

## 放行条件对照

|条件|结果|
|---|---|
|apple 完整行两条|PASS（编号 1/2 各一次）|
|banana 一条|PASS（编号 3）|
|未知词零条|PASS（`(0 rows)`，rc=0）|
|重复查询不丢不重|PASS（第二次 apple 仍 2 行）|
|无业务明文进入服务器|PASS（data/index 全为 Base64 封装，泄露扫描 OK）|

## 签收区（接手人填写）

- 接收状态：待组长填写「已接收」或「退回原因」。
- 复核要点：用 `evidence/live-full.log` 对照五条放行条件；确认未支持语法 rc=3 且无部分明文；按复现命令重跑。
