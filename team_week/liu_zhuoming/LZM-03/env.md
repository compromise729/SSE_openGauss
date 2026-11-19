# LZM-03｜完整复跑步骤

先按 [契约修复与原生复验](契约修复与原生复验.md) 固定源码版本。以下命令在 CentOS 的独立、已配置构建目录里复验同批 `gaussdb`、`gsql`、`libpq` 和 `SSE_v1.sql`。

## 1. 打开终端并使用已有运行配置

Windows PowerShell：

~~~powershell
wsl.exe -d CentOS7 -u root
~~~

进入 Linux 后执行：

~~~bash
repo=/opt/og/lzm03-pr11-review.Gtp3l3v2/source
cd "$repo"
source /opt/rh/rh-python36/enable
export CXX=/opt/og/binarylibs/buildtools/centos7.6_x86_64/gcc7.3/gcc/bin/g++
export LD_LIBRARY_PATH="$(dirname -- "$("$CXX" -print-file-name=libstdc++.so)")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
tools="$repo/team_week/liu_zhuoming/LZM-03/preflight"
~~~

在其他机器上，将 `repo` 改为实际的独立构建目录。若该目录还未生成同批产物，先在已配置的构建目录执行原生构建。

~~~bash
runuser -u omm -- bash -ec '
  cd "$1"
  source /home/omm/.sse_opengauss_env
  make -j6
  make install prefix="$PWD/dest"
' _ "$repo"
~~~

## 2. 一次运行全部验证

~~~bash
log=$(mktemp /var/tmp/state-release-run.XXXXXXXX.log)
rc=0
bash "$tools/verify-all.sh" > "$log" 2>&1 || rc=$?
cat "$log"
printf '完整验证退出码=%s
运行日志=%s
' "$rc" "$log"
~~~

这一步自动完成：

1. 以 omm 在新私有目录运行 18 个模块用例。
2. 从独立构建目录加载正常安装的 gsql、gaussdb、libpq，仅编译初始化和故障注入工具。
3. 在私有挂载命名空间创建个人实例，显式准备 Fides 基础密钥和新版状态。
4. 从同一源码目录导入 SSE_v1.sql。
5. 运行 18 项真实客户端用例，保存 stdout、stderr、调用事件和 JSON。
6. 停止本次实例，比较安装程序/库以及原客户端、服务端目录的摘要。

最终预期：

~~~text
PASS LIVE-01-create
...
PASS LIVE-15-reinsert-failure
LIVE_RESULT=PASS
ISOLATED_SERVER_STOPPED
LIVE_EXIT=0
RELEASE_RESULT=PASS
MODULE_CASES=18 REAL_REQUEST_CASES=18
完整验证退出码=0
~~~

中途模块日志中的 `RELEASE_RESULT=NOT_RELEASED` 和静态核对的 `REVIEW_REQUIRED` 表示该阶段尚未执行真实请求。判断整次结果使用末尾的 RELEASE_RESULT 和退出码，以及 release.json。

## 3. 路径和结果文件

下表是原始临时构建的历史位置。当前原生复验的路径以脚本新打印的 `RELEASE_EVIDENCE_DIR`、`LIVE_EVIDENCE_DIR` 和 `RUN_DIR` 为准。

| 输出 | 本轮路径与含义 |
| --- | --- |
| RELEASE_EVIDENCE_DIR | /var/tmp/state-release-check.AjvXiziG，总日志和 release.json。 |
| build/ | 上述目录下的临时 gsql、gaussdb、共享模块和注入库。 |
| 模块证据 | /var/tmp/state-crypto-test.EfWf2Wax，含 18 个结果与私有密钥现场。 |
| LIVE_EVIDENCE_DIR | /var/tmp/state-live-evidence.6dVdG2eV，含真实运行日志与环境比较。 |
| RUN_DIR | /var/tmp/fides-isolated.KvNT98Su，个人实验数据库与两侧状态。 |
| 新版状态 | RUN_DIR/evidence/v1-state，真实 AEAD 密钥、计数和表登记。 |
| 旧 Fides 基础密钥 | RUN_DIR/client，通过私有挂载对应编译路径 sse_storage。 |
| 服务端状态 | RUN_DIR/server，通过私有挂载对应 sse_server_storage。 |
| socket | RUN_DIR/socket，客户端显式连接此 Unix socket，关闭 TCP 监听。 |

查看总体结果：

~~~bash
release=$(sed -n 's/^RELEASE_EVIDENCE_DIR=//p' "$log")
cat "$release/release.json"
cat "$release/live-summary.log"
~~~

交付用公开输出已保存于本工具文件夹 [result/](result/)。private/、实际密钥、密钥摘要和故障数据库保留本机，不放入飞书附件。

最终故障用例会故意让索引重插失败，实例正常停机后保留标记和故障现场。它是可丢弃验证实例，不能作为健康环境继续开发。正常客户端重开已在 LIVE-04、LIVE-14 中验证；服务重启恢复属于后续任务。

## 4. 如需分开执行用于排错

只检查模块和源码接入：

~~~bash
rc=0
bash "$tools/verify.sh" || rc=$?
printf '模块及静态核对退出码=%s
' "$rc"
~~~

模块应全部 PASS，静态核对返回 4 表示需要结合真实执行顺序证据。

单独验证已构建的原生产物：

~~~bash
bash "$tools/run-live.sh" --native
~~~

复查原始交付采用的临时替换流程时，使用对应的原提交与原日志。当前工具保留 `verify-all.sh --overlay` 供定位临时构建问题。

## 5. 实际交互请求与故障操作

首次初始化、新版建表、插入和故障命令见 [fault-injection.md](fault-injection.md)。复验接手人公布的新提交时，记录新提交号和源码指纹，再完整运行第 2 步；旧证据保留于 result/before-integration/。
