# ZYC-01 交接记录：移植旧客户端防护并准备接入位置

> 任务：ZYC-01（截止 2026-11-18 10:00，前置 SHY-02）。负责人：张永昌。
> 正式状态为已合入。原始防护通过 [PR #4](https://github.com/compromise729/SSE_openGauss/pull/4) 合入，客户端补修通过 [PR #9](https://github.com/compromise729/SSE_openGauss/pull/9) 合入。CentOS 已在补修版本通过 11 项 gsql 实库回归。新版用户 SQL 的密文写入与查询展示由 ZYC-02/03 实现。
> 原始交付采用基准、补丁和文件摘要。团队契约基线为 `7bace5b2182552f1e2fe5865ac66d58812435510`，ZYC-01 补修被测提交为 `e986a9879e8d901d00736d1818101b4f1cc19e15`，合入提交为 `b7f27558949a9ba8ac1f1c2e0818d91aed3cb398`。LZM-03 联测请使用 `c3fa234db4531e8de1aca2314e40a9ea0dec828e`。

## 1. 交接要素

|项|内容|
|---|---|
|前置任务编号|**SHY-02**（契约 v1.0 已发布）；其输入为 SHY-01 统一基线 25489f5 与 LZM-01|
|实际输入提交号|源基线 `25489f563f25b522d0cd37a6c85865039e3f340f`；SHY-02 发布提交 `b6159cf8d0dd6d3d8befa8fd84816d289a7b88d1`（分支 team/shy02-contract-v1，release_repository `/opt/og/shy02-release.K4AiadZr/source`）|
|合同版本|**v1.0**（`include/sse_v1_contract.hpp`，`kProtocolVersion = 1`）|
|本人输出|基准提交 + 两份补丁（`patches/`）+ 调用点说明（`03-调用点说明.md`）+ 离线回归。此项记录作者提交时的交付包|
|接手人|组长施骅洋（本任务交付）；后续由张永昌本人接续 ZYC-02（前置 SHY-03）|

## 2. 改动文件及原因

原交付包提供**可应用于统一基线的两份规范化补丁**，由 SHY-01 已规范化版本（`ZYC-common.patch` / `ZYC-fe-exec.patch`）移植而来：

|补丁|目标文件|改动|原因|
|---|---|---|---|
|`ZYC-01-common.cpp.patch`|`src/bin/psql/common.cpp`|+119/-1（两处 hunk：1503 行后插入 4.1 检查；2415 行附近改写 4.2 重插返回判断）|4.1 单表 Fides 输入拒绝；4.2 重插失败停止第三轮回查，去除假成功|
|`ZYC-01-fe-exec.cpp.patch`|`src/common/interfaces/libpq/fe-exec.cpp`|+13（三处 hunk：标志位、空 token 分支、发送前拦截）|第 5 节未登记词空 token 防护，请求不发送|

两份补丁的正文与本人 `team_prep/zhang_yongchang/patches/` 原始版本**逐字节一致**（证据 `patch-applicability.log`），仅文件头改为统一仓库路径；`fe-exec.cpp` 保留实际 CRLF 上下文（SHY-01 已验证该格式可在 Linux 基线应用）。

**处理与现有代码重复的部分**：SHY-01 基线说明三份成员补丁「待移植、未应用」，本人核对基线源码副本 `common.cpp` / `fe-exec.cpp` 中 `zyc_re_insert_res` / `zyc_invalid_sse_request` / 4.1 guard 标记计数为 0（未重复植入）。两份补丁与 SHY-02 新增的契约头文件 `sse_v1_contract.hpp` 无符号冲突（头文件独立、未改动 common.cpp/fe-exec.cpp 既有函数签名）。移植后新业务加密分支（ZYC-02/03 实现）仍须经过输入检查与错误处理，见 `03-调用点说明.md`。

## 3. 构建 / 运行命令（供接手人复核）

以下命令供历史基线复现原补丁。正式联测从已合入的集成提交开始。

```bash
cd /opt/og/shy02-release.K4AiadZr/source     # 或任一 25489f5 基线副本
patch -p1 --binary < ZYC-01-common.cpp.patch
patch -p1 --binary < ZYC-01-fe-exec.cpp.patch   # fe-exec.cpp 为 CRLF，保留行尾
# 编译安装（本环境 SHY-01 已验证流程；make install 后实例会退出，需重新 gs_ctl start）
make -j4 && make install
```
> 说明：原交付时 VMware CentOS 7 的 SSH 超时，作者当时未在那台虚拟机编译安装；下方离线测试在 Windows 本地 Python 完成。历史真实编译安装记录见 `team_prep/zhang_yongchang/evidence/raw/16-build.txt`（当时基线同一提交、补丁正文同一内容，`MAKE_EXIT=0 / INSTALL_EXIT=0`）。

离线回归（本地，无需数据库）：

```bash
python -m unittest discover -s . -p 'test_*.py' -v
```

SQL 夹具（ZYC-02/03 接口交付后在隔离实例执行）：`zyc01-input-cases.sql`。

## 4. 测试输入与预期 / 实际结果

|用例组|输入|预期|实际|记录|
|---|---|---|---|---|
|补丁可应用性|两份规范化补丁对基线副本 patch --dry-run|全部 hunk 成功|common/fe-exec 均 exit=0，正文与 team_prep 原始补丁逐字节一致|`patch-applicability.log`|
|调用点分流|新密文表/旧 SSE 表单表查询→受保护；普通表/物理表/内部请求→放行|4 类分流正确|4/4 通过|`offline-regression.log`|
|未支持语法|AND/OR/LIKE/投影/非字符串/参数化/UNION/ORDER/LIMIT/缺 WHERE/错列/多语句|发送前拒绝|12/12 REJECT|同上|
|引号内 AND / 中文空格|`'red AND green'`、`'it''s OR not'`、`'中文 关键词'`|按完整字符串|通过，关键词原样保留|同上|
|空 token 分类|健康未登记词 vs 坏状态 vs 已登记但空 token|空集 / state_invalid / protocol_invalid|3/3 通过|同上|
|重插返回状态|NULL、PGRES_FATAL_ERROR、PGRES_COMMAND_OK|停止回查 / 停止回查 / 继续|3/3 通过|同上|
|新版密文 SQL 闭环|新版 CREATE/INSERT/SELECT|由 ZYC-02/03 验收|ZYC-01 实库证据覆盖旧 Fides 防护|正式合入与放行记录见上文和第 7 节|

离线回归合计 **16 个测试全部 OK**。这些是规则模型与错误分支模型，**不是**真实客户端/数据库验证。

## 5. 原始日志路径

- `patch-applicability.log`：补丁一致性比对与 dry-run 结果
- `offline-regression.log`：16 个离线回归用例逐项输出
- 本目录的 `*.py` 脚本：输入分流、重插状态、空 token 分类的可执行模型
- `zyc01-input-cases.sql`：供 ZYC-02/03 联测执行的输入夹具
- 历史真实数据库实测（复用基线，非本子任务新增）：`team_prep/zhang_yongchang/evidence/raw/17-after-checks.txt`（4.1 拒绝与未登记词）、`14-before-checks.txt`（改前假成功）

## 6. 未完成项 / 已知限制（不得宣称已解决）

1. **新版密文 SQL 闭环待接入。** 旧客户端防护已完成实库复验。新密文表写入（ZYC-02）、索引检索到本地明文展示（ZYC-03）待 SHY-03 接口交付后实现。
2. **空 token 语义需配合状态分类**：当前 fe-exec 补丁对空 token 统一报错；ZYC-01 框架已能区分「健康未登记词→本地零行」与「坏状态/异常→失败」，但真实 preflight 状态来自 LZM-02，须在 ZYC-03 接通（缺口 7、缺口 11）。
3. **重插失败防护仅阻止假成功**：补丁停止第三轮回查，但不自动恢复被取出的索引；持久化标记与重启阻断由刘卓明 LZM-02/04 接通（缺口 11）。
4. **不修改他人文件**：Client_Sophos.cpp 的过滤逻辑、startup.cpp 状态读写、密码模块、服务端 SQL 均不在本轮改动范围。
5. **历史遗留失败仍存在**：Mitra 第二关键词 0 行、Mitra JOIN/Diana SIGSEGV、LEFT/RIGHT JOIN 语义差异、同编号重插重复行、重启后 Orion/Mitra 时序问题为基准版原有，见 team_prep 报告，本轮不修复、不宣称已解决。
6. **原交付环境记录。** VMware 曾因 SSH 超时缺少该轮编译证据，组长随后在 WSL CentOS 7 完成编译与实库复验。

## 7. 放行条件核对（任务单）

|条件|现状|
|---|---|
|原有防护已进入统一代码|两份补丁和后续补修已在集成版本编译并通过实库复验。 [PR #4](https://github.com/compromise729/SSE_openGauss/pull/4) · [PR #9](https://github.com/compromise729/SSE_openGauss/pull/9)|
|拒绝输入不发送业务请求、不改变计数|实库复验覆盖单列与多列投影、复合条件等，客户端拒绝且计数摘要不变。|
|普通 SQL 不被误拦截|实库普通表查询通过。含单引号的真实 Fides 关键词也连续命中两次，覆盖索引重插后的查询。|

## 8. 接手与复现

接手人：组长施骅洋。请先读本记录与 `03-调用点说明.md`，从团队远端检出对应固定版本，构建后在 LZM-01 新隔离实例中运行 `team_week/zhang_yongchang/ZYC-01/tests/verify-live.sh`。张永昌接续 ZYC-02 前须等待 SHY-03 发布并复跑其最小用例。

最小复现（无需数据库）：`python -m unittest discover -s . -p 'test_*.py' -v` → 16 OK。
