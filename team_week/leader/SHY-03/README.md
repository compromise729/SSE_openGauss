# SHY-03 客户端接入基线发布

**验收通过，代码已发布到 `main`。** 联测基线为 `ee5775ac8857e6847ad3f4b8ffcf80a477cc7dee`，正式发布提交为 `8111044852770529bfacde4de5af5690dd6a8322`。核对确认两者的 Git 文件树一致。[发布 PR #12](https://github.com/compromise729/SSE_openGauss/pull/12)

| 接收项 | 已合入 / 已复验 |
| --- | --- |
| ZYC-01 客户端防护 | [#4](https://github.com/compromise729/SSE_openGauss/pull/4)、[#9](https://github.com/compromise729/SSE_openGauss/pull/9)。同批 CentOS 程序的 11 项旧入口实库回归通过，16 项离线规则模型在 Windows Python 通过。 |
| YZH-01 随机化认证加密 | [#5](https://github.com/compromise729/SSE_openGauss/pull/5)。226 项常规断言、8 项随机源失败断言和尾行认证失败用例通过。最大合法行业务封装是 45144 个 Base64 字符。 |
| LMD-01/02 数据库 | [#6](https://github.com/compromise729/SSE_openGauss/pull/6)、[#7](https://github.com/compromise729/SSE_openGauss/pull/7)。建表登记与事务回滚通过。真实 Fides 五轮搜索、密文原样存取、按 rid 回查及最大合法载荷均通过。 |
| LZM-02/03 客户端状态 | [#8](https://github.com/compromise729/SSE_openGauss/pull/8)、[#10](https://github.com/compromise729/SSE_openGauss/pull/10)、[#11](https://github.com/compromise729/SSE_openGauss/pull/11)。18 项模块和 18 项原生 gsql 实库请求通过，覆盖密钥初始化、缺钥、状态标记、计数边界和重插回滚。 |

## 构建与复跑

CentOS 7 使用 GCC 7.3，在已配置的隔离源码目录检出联测提交，复用该目录的构建缓存并重新编译本轮变更的 C/C++ 文件。运行 `make -j8` 和 `make install prefix="$PWD/dest"`。隔离数据库进程的实际命令为该目录的 `dest/bin/gaussdb`，gsql 实际加载其 `dest/lib/libpq_ce.so.5`。本次各实例均已停止。[构建指纹与复验结果](构建与验收.json)

- 新版端到端及状态复验按 [LZM-03 原生复跑步骤](../../liu_zhuoming/LZM-03/env.md) 运行 `verify-all.sh`。
- 数据库检查从 LZM-01 新建隔离会话，导入 `sse_sql/SSE_init.sql` 后运行 LMD-01 的 `run-module.sh`。另开新实例导入基础 SQL、运行 `fides_setup;`，使用 LMD-02 的 `run-roundtrip.sh` 和 YZH-01 目录做真实密文往返。[LMD-01 交接](../../li_mengde/LMD-01/handoff.md) · [LMD-02 交接](../../li_mengde/LMD-02/handoff.md)
- 旧客户端实库回归在另一新隔离会话执行 `team_week/zhang_yongchang/ZYC-01/tests/verify-live.sh`。离线规则模型用 Python 3.10 或以上执行 `python -m unittest discover -s team_week/zhang_yongchang/ZYC-01/tests -p 'test_*.py' -v`。[ZYC-01 交接](../../zhang_yongchang/ZYC-01/handoff.md)

## ZYC-02 接手

张永昌从包含 `8111044` 的最新 `main` 切 `feature/ZYC-02-encrypted-write`。如果已有同名分支，把最新 `origin/main` 合入现有分支后复测。

```bash
git fetch origin
git merge-base --is-ancestor 8111044852770529bfacde4de5af5690dd6a8322 origin/main
git checkout -b feature/ZYC-02-encrypted-write origin/main
```

按 [现行接口契约](../SHY-02/接口与数据格式约定.md) 和 [LZM-03 契约修复](../../liu_zhuoming/LZM-03/契约修复与原生复验.md) 接着验收用户 SQL 的密文写入。ZYC-02/03 仍各自完成任务验收。跨端自动恢复和服务端重启后的完整联测按后续任务处理。
