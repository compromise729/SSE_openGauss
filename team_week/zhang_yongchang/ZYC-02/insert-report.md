# ZYC-02｜用户 SQL 到密文写入 报告（insert-report.md）

> 任务：ZYC-02（截止 2026-11-19 12:00，前置 SHY-03）。负责人：张永昌。
> 结论：**在 SHY-03 集成版上完成真实端到端写入验收，全部放行条件通过。**

## 1. 输入与版本

|项|内容|
|---|---|
|前置任务|SHY-03（已验收 PASS，README 明示可接 ZYC-02）|
|实际输入提交|`main` = `c631fbc2be74…`；其含 SHY-03 正式发布提交 `8111044852770529bfacde4de5af5690dd6a8322`（PR #12，与联测基线 `ee5775ac` 树一致）|
|契约版本|SHY-02 v1.0（`sse_v1_contract.hpp`，`kProtocolVersion=1`）；SHY-03 更新 SQL 文本/计数上限/搜索事务约定，字节布局不变|
|本人输出|本目录验收报告 + 构建/运行证据；接入代码已由 SHY-03（LZM-03）落在 `row_request_v1.cpp` 与 `common.cpp`，无另起分支重写|
|构建产物|SSE_v1.sql SHA256 `5c1f413d…bf34`（与 SHY-03 官方记录逐字一致）；gsql `4999e793…dec7a9`；gaussdb `7bbdd1e3…8658`；libpq_ce.so.5.5 `c7da6c48…a413f`|

## 2. 实现归属说明（重要）

逐文件核对 SHY-03 `main` 源码后确认：ZYC-02 要求的用户 SQL→密文写入能力**已由刘卓明 LZM-03 在 `src/common/interfaces/libpq/row_request_v1.cpp` 的 `execute_request()` 中实现并接入 `common.cpp:1494`**，并由组长在 SHY-03 统一验收（DB-01/DB-02/STATE 等 18+18 用例 PASS）。我本人的 ZYC-01 两份补丁也已在 main（`common.cpp` 4.1 guard、重插返回检查；`fe-exec.cpp` 空 token 守卫）。

因此本轮 ZYC-02 的实质工作是**在统一集成提交上复跑并独立验收**，不重复实现同一逻辑，不另造与集成版冲突的源码分支。这符合任务书「已有成果复用基线，最后必须在统一集成版本复验」。

## 3. 写入链路实际核对（源码级）

`execute_request()` 对 INSERT 分支逐项覆盖任务书 6 项要求：

|任务书要求|实现落点（row_request_v1.cpp）|
|---|---|
|三字段 INSERT 解析、引号状态提取|`lex()` + `Parser`，`ind/keyword/payload` 三个 `p.literal()`；按引号状态解析，双单引号转义|
|非业务含义 rid|`new_rid(rid)`（YZH-01 密码随机源，16 字节/32 hex）|
|行与位置加密，全部密码检查成功再推进|`encrypt_row(scope,rid,row,keys,blob)` + `encrypt_locator(scope,{rid,0},keys,position)`，异常经 `checked()` 抛出即中止|
|状态检查与操作标记|`preflight`（v1 状态健康性）→ `begin_checked_operation(...,"insert",...,check_counters)` 持锁重读+额度预检+持久化 `operation.v1`|
|生成更新令牌/按约定发送密文与索引|`client->insertion_request(keyword,...)` 生成 ut；`sse_v1_insert(public,table,rids,data_blobs,uts,locator_blobs)` 一次发送|
|成功清标记、失败留状态|`save_counters` → `complete_operation`；任一步 `Failure` 保留标记|
|新密文表创建入口 + 版本映射|CREATE 分支：`sse_v1_create` + `register_table(scope,kProtocolVersion)`，服务端/本地版本映射同流程|
|停用旧 join_col 路径|v1 分支在 `common.cpp:1503` handled 后直接返回，不进入旧 SSE/join_col 改写逻辑；新版物理表无 join_col 列|
|长度/转义检查在改状态前|`text.size()>6MB`、`kMaxBatchRows`、字段字节上限、SQL 参数数组编码；超限在 begin 前 `fail()`|

## 4. 真实构建

- 环境：VMware CentOS7，GCC 7.3（`/opt/og/binarylibs/buildtools/centos7.6_x86_64/gcc7.3`），独立可写副本 `/home/yonghu/shy03_build`。
- 从 SHY-03 `c631fbc2` 通过 GitHub API 取 19 个变更源码 + 3 个 YZH-01 密码模块文件，覆盖到保留构建缓存的副本，`make -j6`（`MAKE_EXIT=0`）+ `make install`（`INSTALL_EXIT=0`）。
- 证据：`evidence/build-evidence.txt`（产物 SHA256）、`evidence/build-log-tail.txt`。

## 5. 真实写入验收结果

隔离实例由 LZM-01 `isolated.sh init` 在私有挂载命名空间启动（端口 55439，Unix socket），`sse_storage`/`sse_server_storage` 经 bind mount 指向本次 `RUN_DIR`。完整日志见 ZYC-03 目录 `evidence/live-full.log`（写入与检索在同一会话完成）。

|用例|输入|预期|实际|结论|
|---|---|---|---|---|
|建表入口|`CREATE TABLE week_demo (ind text,keyword text SSE='FIDES',payload text) WITH (sse_storage='ROW_AEAD_V1');`|创建 data/index 物理表并登记版本|rc=0，生成 `week_demo_data`/`week_demo_index`，catalog 登记|PASS|
|三行写入|`INSERT ... ('1','apple','red apple'),('2','apple','green apple'),('3','banana','yellow banana')`|整批成功|rc=0，data 3 行、index 3 行|PASS|
|服务端无明文|`SELECT rid,data_blob FROM week_demo_data` / `index`|只有 Base64 密文封装|data_blob 全部 `U1NFMQ…`（SSE1 封装），locator_blob 同；无 ind/keyword/payload 明文；rid 为 32 hex 随机值|PASS|
|状态初始化|`initialize-live`|生成 AEAD 密钥、版本/映射|`INITIALIZE_STATUS=ok`|PASS|
|中文/空格|INSERT `'中文 苹果 space'` 两行|按完整字符串写入|rc=0，2 行|PASS|
|SQL 转义|INSERT `'it''s OR not'`|双单引号正确保留|rc=0，回查得到 `it's OR not` / `o'brien apple`|PASS|
|明文泄露扫描|对 data/index 全表 blob 做关键字匹配|不含 apple/banana/red/green/中文/原始 rid|`NO_PLAINTEXT_IN_DATA_OK`、`NO_PLAINTEXT_IN_INDEX_OK`|PASS|

`/opt/og/openGauss-server/sse_storage` 为新版本地状态目录（由 SSE_V1_STATE_DIR 指定），与 Fides 基础密钥分离；报告只记文件职责与条数，不输出密钥。

## 6. 未完成项 / 边界（不得宣称已解决）

- 写失败后的**跨端索引恢复**不在本任务：重插失败仅保留标记并阻断，不自动补回索引（任务书「必须明确告知」表第 1、2 项）。
- 多库多表并发隔离、计数命名空间重构未完成；本轮限定单环境单张新版逻辑表。
- 服务端重启后的状态延续属后续任务（LZM-04 / SHY-04），本轮未宣称通过。
- 历史遗留（Mitra JOIN、Diana SIGSEGV、LEFT/RIGHT JOIN 语义等）为基准原有，本轮不修复。

## 7. 复现命令（摘要）

```bash
# 在独立可写构建目录（含 SHY-03 c631fbc2 源码）
source /home/omm/.sse_opengauss_env   # 指向本副本工具链
make -j6 && make install prefix="$PWD/dest"
# 隔离实例（root + 私有挂载命名空间）
OPENGAUSS_PROJECT_ROOT=<repo> bash team_week/liu_zhuoming/LZM-01/isolated.sh init --port 55439
# 会话内：fides_setup → SSE_v1.sql → initialize-live → CREATE/INSERT/SELECT
```
