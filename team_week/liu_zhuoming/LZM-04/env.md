# LZM-04｜构建与复跑

## 1. 打开本机 Linux root 终端

Windows PowerShell 中执行：

```powershell
wsl.exe -d CentOS7 -u root
```

进入含 LZM-04 工具的独立、已配置的 SHY-04 构建副本执行。旧构建配置有固定绝对路径时，在私有挂载命名空间内把副本绑定到该路径，再从该目录运行以下命令。

```bash
repo=$(pwd -P)
source /home/omm/.sse_opengauss_env
source /opt/rh/rh-python36/enable
tools="$repo/team_week/liu_zhuoming/LZM-04/preflight"
```

这里 tools 是工具文件夹，其他机器改为自己的实际项目位置；两个 source 读取已有编译器和 Python 3.6 配置，不更改安装版本。

## 2. 独立构建

```bash
build=$(mktemp -d /var/tmp/lzm04-build.XXXXXXXX)
python3 "$tools/build-current.py" --repo "$repo" --out "$build"
```

预期 BUILD 阶段全部通过并输出 LIVE_BUILD_DIR。若构建报错，先查看该目录的 build 日志，不继续运行。

当前安装程序仍是旧产物，因此本轮从当前源码重编受影响单元，复用已有对象缓存；调用已合入的 LZM-03 构建入口。缺失的锁、函数、跟踪头文件在 build/generated 下按项目原生成规则创建；对象清单在内存恢复，不向源码目录写生成文件。这是本机不更换安装环境条件下的验证构建，不是干净原生发布包验收。其他机器需具备团队基线的构建缓存和既有依赖。

构建会记录实际检出提交和固定源码基线 `5c8b2b03b482e203ac65a6f989a1b669126f263c`，并把安装程序、配置、测试程序的 SHA-256 写入 `$build/build-input.json`。验证入口会在启动实例前复核清单。工具可随文档合入 `main`，构建时直接检出含工具的提交即可。

## 3. 运行全部真实用例

```bash
log=$(mktemp /var/tmp/lzm04-run.XXXXXXXX.log)
rc=0
bash "$tools/verify.sh" "$build" > "$log" 2>&1 || rc=$?
cat "$log"
printf '退出码=%s
日志=%s
' "$rc" "$log"
```

脚本以 root 建立私有挂载命名空间，新服务程序只在该空间使用。实际数据库、gsql 和故障操作均以 omm 执行，连接本次 Unix socket，关闭 TCP。每个故障都建立独立数据库和新版状态目录。最后停止实例，保留文件和故障标记，并比较安装程序/库以及隔离外两侧状态的摘要与权限。

每个 PASS 表示注入后的实际结果符合该项断言；故障客户端非零退出是预期，不代表测试失败。总命令要求 36 项全部通过、结果完整且实例停止，成功时返回 0。

```text
PASS 04-01-normal-restart-and-counters
...
PASS 04-36-clear-dir-fsync
LZM04_CASES=36 RESULT=PASS
ISOLATED_SERVER_STOPPED
VERIFY_EXIT=0
```

## 4. 查阅结果

输出 EVIDENCE_DIR 是本次外层日志目录，其中 live.log 显示 RUN_DIR。真实用例结果位于 RUN_DIR/evidence/lzm04/results.json；每次请求的 stdout/stderr 同目录保存。数据库及密钥在个人运行目录内，故障状态不自动清除。

本轮实际路径和结果以 handoff.md、result/input.json 为准；重新运行会生成全新目录。公开交付只复制用例 JSON、公开样例输出、源码指纹和环境比较结论，不复制私钥、密钥摘要、整个数据库或 *.private 文件。

四项交付前闭环的逐用例输入、命令、预期、实际与证据索引见 [闭环核验（第九节）](handoff.md#closed-loop)。实际目录不可写由0500权限用例覆盖；单次文件/目录fsync失败已实测；掉电、磁盘满及复合故障仍为未验证。

本次总入口会复制隔离工具到证据目录并从/var/tmp调用，自动传入实际项目根。已解决问题及3个工具改动见 [handoff.md第十一节](handoff.md#resolved)；无需手动修改原故障目录。

## 5. 最新结果与复现判据

result/只保存本次最新36项验证输出，不再包含补测前历史副本。现有preflight/工具已按第3步重新运行并通过；依赖及构建前提见第1～2步。原始数据库、密钥、状态和故障标记仍在本机个人实验目录，未随历史结果副本一起删除。

复现时应核对：36项PASS、139次gsql请求都有命令/输出、VERIFY_EXIT=0、实例停止、隔离外比较通过；正常查询编号1和2各一次，计数2→4→6→8→10；各故障返回及持续阻断符合results.json的预期。新建实例的目录、PID、数据库OID、密钥和密文是本轮生成的值，不要求与交付日志逐字节相同。密钥/状态不变比较针对同一轮内的重开或重启，不能拿不同新环境的密钥互相比对。
