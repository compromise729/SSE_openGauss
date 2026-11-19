# LZM-02｜隔离、初始化与恢复

使用同一套已构建的 openGauss 服务、Unix socket 和个人实验目录。将 `OPENGAUSS_PROJECT_ROOT` 设为已检出并构建的隔离源码目录。隔离运行入口沿用已合入 main 的 [LZM-01 isolated.sh](../LZM-01/isolated.sh)。LZM-02 的在线验证使用另建的临时实例，旧版客户端状态和新版测试状态在该实例内分开。

## 此模块可以怎样复跑

原版自测输入和产物指纹见 `result/current-input.json`。首次初始化已通过 [PR #10](https://github.com/compromise729/SSE_openGauss/pull/10) 补修。接手人从 `integration/SHY-03` 的 `c3fa234db4531e8de1aca2314e40a9ea0dec828e` 检出并构建，再复跑以下工具。下面是在 WSL 外层 root 终端复跑工具的命令。

~~~bash
cd "$OPENGAUSS_PROJECT_ROOT"
source /home/omm/.sse_opengauss_env
tools="$PWD/team_week/liu_zhuoming/LZM-02/preflight"
bash "$tools/verify-real-keys.sh"
bash "$tools/verify-state.sh"
bash "$tools/verify-bridges.sh"
bash "$tools/verify-prerequisites.sh"
bash "$tools/verify-catalog.sh"
~~~

离线预期打印 `PASS STATE-01` 到 `PASS STATE-12`。原入口接入验证打印 `PASS BRIDGE-01` 到 `PASS BRIDGE-04`，多种入口会重复使用相应用例编号。产物缺失验证打印 `PASS CATALOG-08`。联机验证打印 `PASS CATALOG-01` 到 `PASS CATALOG-07`。状态、桥接和登记脚本会打印 `EVIDENCE_DIR`。真实密钥脚本直接输出断言结果。联机夹具使用公开合成密钥及物理形状 SQL，结束后停止自身实例。

联机验收要求 `src/bin/psql/gsql` 可执行，且 `src/common/interfaces/libpq/libpq_ce.so.5` 可读。缺少必要产物或权限时返回非零，先完成构建再运行。`verify-prerequisites.sh` 用临时假项目路径验证这三个失败分支。用例结果在 `result/`。

## 新版正式入口的接续方式

首次开发时，由集成入口创建空的 `0700` 隔离客户端状态目录，连接本次 Unix socket，用真实的 YZH-01 密钥初始化函数调用 `initialize_state`。同一套环境中登记新版表必须持有 `create` 操作，服务端建表事务成功后再写本地版本与表映射。当前仓库的旧版 `init` 会在其客户端目录生成旧密钥。联机验证因此在另一个空目录放置新版夹具。新版业务入口接入时使用专用空状态目录。`initialize_state` 在其私有空子目录调用真实 YZH-01 回调，校验后发布三份文件，最后写 `environment.v1`。

后续开发时保留同一实例、客户端目录和服务端目录。显式选择该新版路径，启动时核对同连接的实例、数据库及表版本。看到 `incomplete_operation`、`key_invalid` 或 `state_invalid` 时停止，保留原目录和诊断。清计数、删标记、重新导入 SQL 或更换密钥会丢失恢复和排查依据。

目前重编的 gsql 会验证新版状态，检查通过后仍以 `unsupported` 停止业务入口，等待 ZYC 路由接入。这个闸门防止通过新版检查后落入旧客户端分支。只读诊断可以用独立检查器。在已打开的 omm 隔离会话中设置下列路径，把 `state` 改成实际新版状态目录，再执行：

~~~bash
tool_dir="$OPENGAUSS_PROJECT_ROOT/team_week/liu_zhuoming/LZM-02/preflight"
state="$ISOLATED_RUN_DIR/evidence/v1-state"
conn="host=$ISOLATED_RUN_DIR/socket port=$ISOLATED_PORT dbname=postgres user=omm"
checker=$(mktemp "$ISOLATED_RUN_DIR/evidence/check-state.XXXXXXXX")
bash "$tool_dir/build-checker.sh" "$checker" && chmod 0700 "$checker" &&
  "$checker" --state-dir "$state" --conninfo "$conn" --table week_demo --keyword apple
echo "退出码=$?"
~~~

`state` 是由 omm 持有的新版状态目录。`conn` 形如 `host=/var/tmp/本次运行目录/socket port=55432 dbname=postgres user=omm`。以目录拥有者运行。退出 0 表示表和词已登记，1 表示未登记，2 表示参数错误，3 表示状态或连接异常。JSON 报告具体错误类别。密钥配对、服务端数据内容和跨端一致性需联机集成后验证。
