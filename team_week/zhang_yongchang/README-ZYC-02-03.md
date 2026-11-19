# ZYC-02 / ZYC-03 交付与复验

对应 [PR #14](https://github.com/compromise729/SSE_openGauss/pull/14)。沿用 SHY-03 已集成的 `row_request_v1.cpp`、客户端入口及密码/状态/数据库模块；本轮修复验收工具和交付材料。

应用基线为 `c631fbc2be747e3611f00ff521ae4dd306cc2789`，包含 SHY-03 发布 `8111044852770529bfacde4de5af5690dd6a8322`。本轮实际被测提交、工具和程序 SHA-256 以 [verified/summary.json](ZYC-03/evidence/verified/summary.json) 为准。交付文档提交可后于被测工具提交，用该清单核对工具字节。

## 一次运行完整验收

使用按 SHY-03 配置并原生构建、安装完成的独立源码副本。构建仍执行 `make -j6` 和 `make install prefix="$PWD/dest"`，编译与数据库以 omm 运行；构建环境见 [SHY-03](../leader/SHY-03/README.md)。

在 **WSL CentOS7 外层 root 终端**，将 `repo` 指向实际源码/同批安装目录：

```bash
repo=/path/to/configured/SSE_openGauss
bash "$repo/team_week/zhang_yongchang/tools/zyc_live_verify_all.sh" --project-root "$repo"
```

入口自动选择 Python 3.6+、使用 omm 的编译环境，编译 `initialize-live`、独立解密工具及公开夹具记录器，运行 3 项入口回归、18 项模块用例、18 项上游真实客户端用例、13 项 ZYC 实库验收。支持含空格路径；默认新建证据目录和隔离实例，不复用旧 PASS 文件。

最后必须同时满足退出码 0、`EVIDENCE_VALIDATED=PASS` 和 `ZYC_RESULT=PASS`。任何 SQL、初始化、输出断言或阶段核对失败均非零退出，详见打印的 `ZYC_EVIDENCE_DIR`。可选 `--evidence-dir NEW_DIR`、`--port 55443`；证据目录必须不存在。

## 成果与证据

- [写入报告](ZYC-02/insert-report.md)、[查询报告](ZYC-03/search-report.md)、两份 handoff 和 manifest。
- [本轮实库用例结果](ZYC-03/evidence/verified/requests/results.json)：完整行、重开/追加、中文/转义/空串、最大载荷、无副作用拒绝、登记/写失败、末行认证失败、缺行和持久化阻断。
- [阶段证据](ZYC-03/evidence/verified/requests/phases.json)：真实 libpq 调用中的令牌请求、加密位置返回、独立认证恢复的 rid、重插和定向回查集合；每个请求的原始输出及记录在同目录。
- [独立解密](ZYC-03/evidence/verified/requests/independent-decode.json)：独立客户端进程解密实际物理业务行及位置，与三条输入逐字段比较。
- `evidence/` 中原有 build/write/live 日志为作者首次交付历史记录；本轮结论使用 `evidence/verified/`。历史记录中旧作者路径不是当前运行依赖。

公开夹具可记录指定明文、token、rid 和密文；派生密钥值已遮盖，不输出原始密钥或密钥摘要，不导出私有状态。libpq 边界记录用于本轮链路核对，不代表网络抓包、TLS 验收或完整安全证明。

多表/多库计数命名空间、并发隔离、自动跨端恢复、密钥恢复/轮换、服务端重启、历史 SSE 方案问题及性能/TLS 验收仍按后续任务处理。本轮正常重开指重启客户端进程。

飞书原交付位置：[ZYC-02](https://nankai.feishu.cn/drive/folder/Rb8vfMFh1lXiG3dlH9Cc46U7nFe) · [ZYC-03](https://nankai.feishu.cn/drive/folder/GyOVfZvszlW1P6dgtdCci4Xwnoa)。本次 Git 修复未代写飞书签收。
