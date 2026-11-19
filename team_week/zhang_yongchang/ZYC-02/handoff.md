# ZYC-02 交接记录：完成用户 SQL 到密文写入

|项|内容|
|---|---|
|前置任务编号|**SHY-03**（已验收 PASS；README 明示 ZYC-02 从含 `8111044` 的最新 main 接续）|
|实际输入提交号|`c631fbc2be74…`（main）；含 SHY-03 正式发布 `8111044852770529bfacde4de5af5690dd6a8322`（PR #12）|
|合同版本|SHY-02 v1.0（`sse_v1_contract.hpp`, kProtocolVersion=1）；SHY-03 搜索/计数约定|
|本人输出提交号/补丁|无独立源码提交（接入代码已在上述 main 提交中，由 SHY-03/LZM-03 落地并经我逐项核对）；本目录交付验收报告 + 证据。`SSE_v1.sql` SHA256 `5c1f413d…bf34` 与 SHY-03 官方一致，可作为本次输入指纹|
|改动文件及原因|本轮不改源码。`row_request_v1.cpp`/`common.cpp` 的写入实现、本人 ZYC-01 的两份防护补丁均已在输入提交；逐项核对确认无重复植入、无与契约符号冲突|
|构建命令|独立可写副本 `/home/yonghu/shy03_build`，GCC7.3 环境：`make -j6` → `make install prefix=$PWD/dest`；结果 `MAKE_EXIT=0`/`INSTALL_EXIT=0`|
|测试输入与预期/实际|建表、三行 INSERT、中文/空格、SQL 转义、物理表明文扫描——全部 PASS，详见 `insert-report.md` 第 5 节与 `evidence/`|
|原始日志路径|`evidence/build-evidence.txt`（产物 SHA256/退出码）、`evidence/build-log-tail.txt`；检索与建表同会话日志在 `../ZYC-03/evidence/live-full.log`|
|未完成项|跨端索引恢复、多表并发隔离、重启延续、历史遗留 SSE 方案问题——见 `insert-report.md` 第 6 节|
|接手人|组长施骅洋验收；张永昌接续 ZYC-03（同批已完成）；李孟德、严子涵协助定位|
|复现命令|见 `insert-report.md` 第 7 节；隔离实例用 `LZM-01/isolated.sh init --port 55439`|

## 签收区（接手人填写）

- 接收状态：待组长填写「已接收」或「退回原因」。
- 复核要点：确认输入提交 `c631fbc2` 含 `8111044` 与本人 ZYC-01 补丁；核对 `SSE_v1.sql` 指纹；按复现命令重跑建表与三行写入。
