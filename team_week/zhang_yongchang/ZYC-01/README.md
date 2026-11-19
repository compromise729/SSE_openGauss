# ZYC-01 交付包索引

任务：**ZYC-01 移植旧客户端防护并准备接入位置**（前置 SHY-02，契约 v1.0）。
负责人为张永昌。防护已合入 `integration/SHY-03`，CentOS 的 11 项 gsql 实库回归及 16 项离线用例均通过。ZYC-02/03 负责新版密文写入与查询展示。 [PR #4](https://github.com/compromise729/SSE_openGauss/pull/4) · [PR #9](https://github.com/compromise729/SSE_openGauss/pull/9)

## 交付文件

|文件|作用|
|---|---|
|`handoff.md`|交接记录：前置/输入提交/契约版本/输出/改动原因/构建运行命令/测试输入预期实际/日志路径/未完成项/接手人/复现|
|`03-调用点说明.md`|按契约区分新密文表/旧 SSE 表/普通 SQL/内部请求，列出写入前与最终展示前的调用位置|
|`ZYC-01-common.cpp.patch`|4.1 输入拒绝 + 4.2 重插失败停止回查（目标 common.cpp）|
|`ZYC-01-fe-exec.cpp.patch`|第 5 节未登记词空 token 防护（目标 fe-exec.cpp，CRLF）|
|`sql_input_guard.py`|输入分类规则模型（离线）|
|`reencrypt_result_guard.py`|重插返回状态规则模型（离线）|
|`test_zyc01_regression.py`|ZYC-01 离线回归：调用分流/未支持语法/引号内 AND/中文空格/空 token 分类/重插状态|
|`zyc01-input-cases.sql`|供 ZYC-02/03 联测执行的输入夹具|
|`patch-applicability.log`|补丁一致性比对与 dry-run 结果|
|`offline-regression.log`|16 个离线回归用例输出|
|`manifest-sha256.txt`|本交付包全部文件的 SHA-256|

## 快速复核

```bash
python -m unittest discover -s . -p 'test_*.py' -v   # 期望 Ran 16 tests ... OK
```

当前联测直接检出组长发布的集成提交。以下命令仅用于在历史基线复现原始补丁，执行前将原始补丁复制到隔离源码根目录。

```bash
patch -p1 --binary < ZYC-01-common.cpp.patch
patch -p1 --binary < ZYC-01-fe-exec.cpp.patch
make -j4 && make install
```

## 明确未解决 / 范围外

真实数据库全链路（ZYC-02/03）、空 token 与持久化状态细分（LZM-02/03）、重插失败跨端恢复、Mitra JOIN / Diana 等历史遗留，均未在 ZYC-01 解决，详见 `handoff.md` 第 6 节与任务单「必须明确告知的未解决部分」。
