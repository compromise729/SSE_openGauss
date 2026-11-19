# LZM-01｜隔离环境与前置检查实测报告

交接对象为组长、李孟德、严子涵、张永昌。本机已验证目录定位、新建与恢复、preflight 退出码和只读性。各组员的环境需分别复跑。任务完成内容和文件清单见 [README.md](README.md)，手动操作见 [env.md](env.md)。

用例清单和可执行脚本见 [preflight/cases.md](preflight/cases.md)。实测输出已随工具文件夹保存到 [preflight/result/](preflight/result/)，本机临时证据目录用于追溯原始记录。

## 执行信息

| 项目 | 本次记录 |
| --- | --- |
| 日期和环境 | 2026-11-17，WSL CentOS7，数据库和检查器以 `omm` 运行。 |
| 使用版本 | 源码基准 `25489f563f25b522d0cd37a6c85865039e3f340f` + 当前交付的隔离入口和验收脚本。检查器、公开夹具沿用 plan2，离线用例执行器改为从同目录找到检查器。 |
| 离线输入 | 公开夹具，默认表 `fides_demo` 和词 `apple`，特定输入在用例中列出。 |
| 联机输入 | `seed.sql` 写入的 `team_fides_demo` 公开样例，已登记词 `teamdemo20261012apple`，未登记词 `teamdemo20261012unlisted`。 |
| 离线原始输出 | `/tmp/fides-preflight.nWOLkFsq` 下的 `executed`、`argument.stderr`、`before` 和 `after`。 |
| 联机原始输出 | `/var/tmp/fides-isolation-check.oZy9oKrf` 下的启动、恢复、异常副本日志和外层快照。两侧状态快照在本次 `RUN_DIR/evidence/` 里。 |

## 何时复跑检查器与验收脚本

`init` 和 `resume` 的日常前置检查直接使用 `isolated.sh preflight`。如果结果与预期不同，先核对表、关键词、目录和原诊断。怀疑检查器本身有问题时，用公开夹具复跑离线验证。只有需要复现隔离、停机或恢复问题，或做正式交付验收时，才跑整套联机验收。

先进入 **WSL 外层 root 终端**，设置本机的项目及工具路径。

~~~bash
repo=/opt/og/openGauss-server
entry=/opt/og/openGauss-server/team_week/liu_zhuoming/LZM-01
cd "$repo"
~~~

**怀疑只读检查器的结果异常**时，执行离线验证。它在临时目录生成夹具，检查退出码和 JSON，并比较检查前后的文件快照。预期打印 `PASS T2-01` 至 `PASS T2-19` 和 `PASS OFF-01`，完整证据路径见 `OFFLINE_EVIDENCE`。

~~~bash
bash "$entry/preflight/verify-offline.sh"
~~~

**要复现隔离、停机或恢复问题，或做正式交付验收**时，执行联机验证。它另建实例并使用公开样例，预期打印 `PASS LIVE-01` 至 `PASS LIVE-10`。本次证据见 `EVIDENCE_DIR` 和 `RUN_DIR`。比对外层原目录时，避免同一时间有其他实验向这两个外层状态目录写入。

~~~bash
bash "$entry/preflight/verify-live.sh"
~~~

## 离线 preflight 用例

T2-01～T2-19 各执行两次，脚本核对预期退出码与 JSON 后逐项打印 `PASS`。每一例的完整执行命令见 [离线 JSONL](preflight/result/offline.jsonl) 的 `argv` 字段。OFF-01 的执行步骤在 [verify-offline.sh](preflight/verify-offline.sh)。

| 用例 | 输入或前提 | 预期结果 | 实测退出码与关键输出 | 结论 |
| --- | --- | --- | --- | --- |
| T2-01 | 正常夹具 | 两个目标已登记 | 0，词计数 2，表映射 `found` | 通过 |
| T2-02 | 计数和映射均为空文件 | 两个目标未登记 | 1，两份文件均为 `unregistered`，`empty=true` | 通过 |
| T2-03 | 正常夹具，词 `unlisted` | 仅词未登记 | 1，计数 `unregistered`，表 `found` | 通过 |
| T2-04 | 正常夹具，表 `unlisted` | 仅表未登记 | 1，计数 2，表 `unregistered` | 通过 |
| T2-05 | 词 `green apple` | 保留词中的空格 | 0，计数 1，表映射 `found` | 通过 |
| T2-06 | 非数字计数 | 报告格式错误 | 3，计数第 3 行要求十进制数字 | 通过 |
| T2-07 | 计数记录截断 | 报告格式错误 | 3，计数第 3 行缺少结尾换行 | 通过 |
| T2-08 | 缺少末尾计数 | 报告格式错误 | 3，计数第 3 行缺少计数或分隔空格 | 通过 |
| T2-09 | 计数超出整数上限 | 拒绝越界值 | 3，计数第 1 行超出 `0..2147483647` | 通过 |
| T2-10 | 计数为负数 | 拒绝负数 | 3，计数第 1 行要求非负整数 | 通过 |
| T2-11 | 已登记词计数为 0 | 返回实际计数 0 | 0，计数状态 `found`，值为 0 | 通过 |
| T2-12 | 已登记词处于整数上限 | 接受上限 | 0，计数为 `2147483647` | 通过 |
| T2-13 | 缺少计数文件 | 报告计数文件缺失 | 3，计数 `missing`，表 `found` | 通过 |
| T2-14 | 缺少映射文件 | 报告映射文件缺失 | 3，计数 2，表 `missing` | 通过 |
| T2-15 | 两份文件均无读权限，以 `omm` 检查 | 报告不可读 | 3，两份文件均为 `unreadable`，`Permission denied` | 通过 |
| T2-16 | 映射缺少有效列项 | 报告格式错误 | 3，映射第 2 行列项格式错误 | 通过 |
| T2-17 | 映射使用未定义方案 | 报告格式错误 | 3，映射第 1 行的方案未定义 | 通过 |
| T2-18 | 表有重复映射 | 返回最后登记的该表映射 | 0，映射返回一列 `keyword`，方案 `fides` | 通过 |
| T2-19 | 计数含无效编码 | 报告编码错误 | 3，计数第 3 行不是有效 UTF-8 | 通过 |
| OFF-01 | 缺少 `--keyword` | 参数错误，诊断在 stderr | 2，stdout 为空，stderr 提示缺少参数 | 通过 |

完整输出见 [offline.log](preflight/result/offline.log)。下面是实测节选。夹具前后的文件清单、权限、属主和 SHA-256 比对一致。

~~~text
PASS T2-01: normal, exit=0
PASS T2-03: normal, exit=1
PASS T2-15: unreadable, exit=3
PASS: 19 cases, 38 checker runs
check_fides_state.py: error: the following arguments are required: --keyword
PASS OFF-01: missing --keyword, exit=2; stdout empty; fixture tree unchanged
~~~

## 联机用例

以下的 `preflight` 指隔离会话中的 `isolated.sh preflight`。常用检查命令见 [日常操作手册](env.md)，完整执行顺序见 [verify-live.sh](preflight/verify-live.sh)。

| 用例 | 命令或输入 | 预期结果 | 实测结果和证据 | 结论 |
| --- | --- | --- | --- | --- |
| LIVE-01 | `isolated.sh init`，初始化前两侧为空 | 前置检查返回 3，随后启动独立实例 | `PREFLIGHT_EXIT=3`，两文件均为 `missing`。socket 可用，TCP 关闭。见 `init.log` | 通过 |
| LIVE-02 | 导入 `seed.sql` 后 preflight 已登记表和 apple 关键词 | 返回 0，计数 2 | 0，`counter.status=found`、`counter.count=2`、`mapping.status=found`。见 `init.log` | 通过 |
| LIVE-03 | preflight 使用同一表和未登记词 | 返回 1 | 1，`counter.status=unregistered`、`mapping.status=found`。见 `init.log` | 通过 |
| LIVE-04 | preflight 仅传 `--table team_fides_demo` | 返回 2，诊断在 stderr | 2，stdout 为空。见 `RUN_DIR/evidence/argument.stderr` | 通过 |
| LIVE-05 | 对两侧状态做检查前后快照，停机后比较外层快照 | 两侧独立挂载，两组文件摘要一致 | 两组 `cmp` 返回 0。见 `RUN_DIR/evidence/preflight.before`、`preflight.after` 及联机证据下的 `outer.before`、`outer.after` | 通过 |
| LIVE-06 | `stop` 后用同一 `RUN_DIR` 执行 `resume`，再用 `resume.sql` 查询 | 原状态仍可用，apple 查询返回 1、2 各一次 | 恢复前返回 0，查询前 6 个状态文件摘要一致，查询返回 1、2 各一次，计数由 2 变为 4。见 `resume.log`、`RUN_DIR/evidence/resume-snapshot.log` | 通过 |
| LIVE-07 | 在缺计数文件的停机副本上执行 `resume` | 返回 3，拒绝启动，保留现场 | 返回 3，报 `missing`，文件摘要一致。见 `missing.log` | 通过 |
| LIVE-08 | 在计数文件被截断的停机副本上执行 `resume` | 返回 3，拒绝启动，保留现场 | 返回 3，报 `format_error`，文件摘要一致。见 `truncated.log` | 通过 |
| LIVE-09 | 在缺少客户端私钥的停机副本上执行 `resume` | 前置检查仍可返回 0，恢复入口拒绝启动 | `PREFLIGHT_EXIT=0`，恢复入口返回 4，文件摘要一致。见 `missing-key.log` | 通过 |
| LIVE-10 | 恢复后令隔离会话以 29 退出 | 原样返回退出码并停机 | 返回 29，打印 `ISOLATED_SERVER_STOPPED`，本次实例 PID 文件已移除。见 `exit-failure.log` | 通过 |

联机验收在导入公开样例后运行 LIVE-02～LIVE-05。脚本将实际退出码、参数诊断和文件快照分别与预期比对后输出 `PASS`，所以符合预期的 `1` 和 `2` 也算通过。以下是 `init.log` 中的逐项输出。

~~~text
PASS LIVE-02: registered table and keyword, exit=0
PASS LIVE-03: unregistered keyword, exit=1
PASS LIVE-04: missing --keyword, exit=2
PASS LIVE-05: isolated state snapshots match
~~~

完整用例输出见 [live.log](preflight/result/live.log)，前置检查的 JSON 与参数诊断见 [live-preflight.jsonl](preflight/result/live-preflight.jsonl)。

### 实测启动与查询输出节选

路径摘自本次 `init.log`。`SOCKET` 是目录，本次 socket 文件位于 `/var/tmp/fides-isolated.B8eFsFSf/socket/.s.PGSQL.55432`。

~~~text
RUN_DIR=/var/tmp/fides-isolated.B8eFsFSf
INSTANCE=/var/tmp/fides-isolated.B8eFsFSf/db/instance
SOCKET=/var/tmp/fides-isolated.B8eFsFSf/socket
PORT=55432
CLIENT=/var/tmp/fides-isolated.B8eFsFSf/client (mounted at /opt/og/openGauss-server/sse_storage)
SERVER=/var/tmp/fides-isolated.B8eFsFSf/server (mounted at /opt/og/openGauss-server/sse_server_storage)
~~~

恢复后 `resume.log` 中的公开查询输出如下。计数增长来自 Fides 查询，摘要校验在查询前完成。

~~~text
 1   | teamdemo20261012apple | red apple
 2   | teamdemo20261012apple | green apple
~~~

目录定位也单独从 `/tmp` 调用仓库内的入口验证过。用原目录恢复后，数据库报告的实例路径和端口与启动输出一致，两侧挂载及停机均通过。见 `/tmp/fides-locations.WGFrnUDt`。

检查器的范围是客户端计数和表映射。恢复入口额外检查所需密钥文件是否存在、可读且非空。密钥有效性、配对及跨端状态一致性未在本任务中验证。
