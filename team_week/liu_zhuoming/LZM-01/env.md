# LZM-01｜日常隔离启动与前置检查

| 什么时候 | 执行哪套流程 | 真正开始实验前确认什么 |
| --- | --- | --- |
| 第一次开发，需要一套空环境 | 准备终端后走**流程 A**。创建独立环境，只初始化一次。 | 空环境的启动前检查预期返回 `3`。准备好实验表和关键词后，做一次只读检查，预期返回 `0`，再继续实验。 |
| 后续开发，已有上次正常停机的环境 | 准备终端后走**流程 B**。填写原 `RUN_DIR`，从原数据库和两侧状态继续。 | 恢复入口自动做前置检查。预期返回 `0` 并进入隔离会话，然后用原状态继续实验。 |

日常只走其中一套。交付时每位组员按文末 [放行验收](#放行验收) 在自己的环境复跑。其他排查用例见 [handoff.md](handoff.md#何时复跑检查器与验收脚本)。

## 共用准备

在 **Windows PowerShell** 进入 WSL。`CentOS7` 是本机发行版名，组员按第一条命令列出的实际名称填写。

~~~powershell
wsl.exe --list --verbose
wsl.exe -d CentOS7 -u root
~~~

在 **WSL 外层 root 终端** 设置下面两个路径。已在同一终端设置过时，可以直接进入流程 A 或 B。

~~~bash
id -un
repo=/opt/og/openGauss-server
entry=/opt/og/openGauss-server/team_week/liu_zhuoming/LZM-01
cd "$repo"
~~~

`id -un` 应输出 `root`。`repo` 是项目根目录，提供 `dest/bin/` 下的 openGauss 和 `sse_sql/SSE_init.sql`。`entry` 是存放 `isolated.sh` 和 `preflight/` 的**工具文件夹**，可以在仓库外。上面两个值已填本机实际路径，组员按自己的位置改。入口通过脚本位置找到检查器，通过项目根目录找到程序和初始化 SQL。需要准备 openGauss、`omm` 或运行配置时，见 [环境配置.md](../../../环境配置.md)。

## 流程 A：第一次初始化新环境

### A1. 新建隔离环境

在 **外层 root 终端** 执行：

~~~bash
cd "$repo" && bash "$entry/isolated.sh" init
~~~

入口会自动创建新的个人实验目录，将其中的 `client` 和 `server` 分别挂载到 Fides 使用的两个固定路径。它在初始化数据库之前先运行只读检查。空目录的两个状态文件尚未生成，预期出现 `PREFLIGHT_EXIT=3`。随后应打印目录信息和 `In this omm shell`，表示本次实例已启动，终端已切换到 **omm 隔离会话**。保存本次的 `RUN_DIR`，后续恢复时输入这个值。

### A2. 初始化 Fides 并写入数据

执行完 A1 后，等到 `In this omm shell` 和 `bash-4.2$` 出现。`bash-4.2$` 是本机的 **omm 隔离终端**，可以在这里执行 `isolated.sh`。如果提示符是 `postgres=#` 或 `postgres-#`，你仍在 gsql 内，请看下方「已经进入 gsql 时怎么继续」。

**方式一：用公开样例，直接从 omm 隔离终端执行。** A1 后仍在空实例、还未导入初始化 SQL 时，直接粘贴下面两行。`id -un` 应输出 `omm`。第二行会自动打开 gsql、导入 `SSE_init.sql`、执行 `fides_setup`、建表并插入数据，结束后回到 `bash-4.2$`。

~~~bash
id -un
isolated.sh gsql -f "$(dirname "$ISOLATED_ENTRY")/preflight/seed.sql"
~~~

预期看到 `fides_setup` 返回 `ok`，随后是建表和插入成功的输出。回到 `bash-4.2$` 后继续 A3。

**方式二：手动初始化自己的数据。** 先在 `bash-4.2$` 执行：

~~~bash
isolated.sh gsql
~~~

等看到 **`postgres=#`** 后执行下面两行。预期第一行创建项目函数，第二行返回 `ok`。

~~~sql
\i :SSE_INIT_SQL
fides_setup;
~~~

继续在 `postgres=#` 执行你自己的建表和初始写入 SQL，记下实际表名和关键词。执行下面一行退出 gsql，看到 `bash-4.2$` 后继续 A3。

~~~text
\q
~~~

### A3. 回到 omm 隔离终端做前置检查

在 **`bash-4.2$`** 执行下面两行。这里使用公开样例的表和词，用自己的数据时替换这两项。

~~~bash
isolated.sh preflight --table team_fides_demo --keyword teamdemo20261012apple
echo "退出码=$?"
~~~

预期 JSON 显示表和关键词均已登记，退出码为 `0`。空环境启动前的 `3` 和写入初始数据后的 `0` 对应两个阶段。进行 Fides 关键词查询前，在同一个 gsql 会话执行 `fides_setup;`。

### 已经进入 gsql 时怎么继续

- 如果把 `isolated.sh ...` 粘贴到了 gsql，出现 `postgres-#`，单独输入 `\r` 并回车。它只丢弃 gsql 中这几行尚未执行的输入，预期回到 `postgres=#`。
- 如果只是打开了 gsql，还未执行初始化，可输入 `\q` 回到 `bash-4.2$`，再走 A2 的方式一。
- 如果已经导入 `SSE_init.sql`，且 `fides_setup;` 返回 `ok`，接着准备数据即可。若还未建公开样例表，可以在 `postgres=#` 执行以下 SQL，接着用 `\q` 回到 `bash-4.2$`。

~~~sql
CREATE TABLE team_fides_demo (ind text, keyword text SSE='FIDES', payload text);
INSERT INTO team_fides_demo VALUES
  ('1', 'teamdemo20261012apple', 'red apple'),
  ('2', 'teamdemo20261012apple', 'green apple'),
  ('3', 'teamdemo20261012banana', 'yellow banana');
\q
~~~

预期建表成功并插入 3 条。然后执行 A3，样例的 apple 计数应为 `2`。

## 流程 B：后续恢复原环境

### B1. 使用原目录启动并自动检查

确认上次已正常停机。在 **外层 root 终端** 执行。把第一行改为上次的完整 `RUN_DIR`，表和词也换成上次实验中已登记的值。下面以本机的公开样例为例。

~~~bash
run='/var/tmp/fides-isolated.UZG7zvVW'
printf 'RUN_DIR=%s\n' "$run"
cd "$repo" && bash "$entry/isolated.sh" resume "$run" --table team_fides_demo --keyword teamdemo20261012apple
~~~

入口会核对数据库已正常停机，重新挂载原来的两侧状态目录，然后在启动数据库前自动检查指定的表和关键词。**看终端输出就能判断结果，`PREFLIGHT_EXIT=0` 和 `In this omm shell` 都出现后再进入 B2。**

| 顺序 | 要看哪部分输出 | 怎样判断 |
| --- | --- | --- |
| 1. 确认只读检查通过 | `PREFLIGHT_EXIT=0` 及紧随其后的 JSON | `counter.status` 和 `mapping.status` 均为 `found`，对应的 `target` 是你在命令里填的关键词和表名。`counter.count` 是已有计数，公开样例首次查询前为 `2`，其他实验以自己的已保存状态为准。`RECORD=` 后是这次的 JSON 记录路径。 |
| 2. 确认用的是原环境 | 脚本启动后打印的 `RUN_DIR`、`INSTANCE`、`SOCKET`、`PORT`、`CLIENT` 和 `SERVER` | 看检查输出**之后**打印的 `RUN_DIR`，应与上次保存的目录完全相同。实例、socket 和两侧实际目录应位于这个目录下，`PORT` 应沿用上一次。 |
| 3. 确认启动就绪 | `In this omm shell`，随后是本机的 `bash-4.2$` | 脚本已经确认所需密钥文件存在、可读且非空，数据库通过 socket 连通，当前终端进入 `omm` 隔离会话，可以执行 B2。 |

下面节选自一次成功恢复的实测。实测路径仅供对照，按自己终端打印的路径判断。

~~~text
PREFLIGHT_EXIT=0 RECORD=/var/tmp/fides-isolated.B8eFsFSf/pre-resume.Qmxm17TO.json
RUN_DIR=/var/tmp/fides-isolated.B8eFsFSf
INSTANCE=/var/tmp/fides-isolated.B8eFsFSf/db/instance
SOCKET=/var/tmp/fides-isolated.B8eFsFSf/socket
PORT=55432
CLIENT=/var/tmp/fides-isolated.B8eFsFSf/client (mounted at /opt/og/openGauss-server/sse_storage)
SERVER=/var/tmp/fides-isolated.B8eFsFSf/server (mounted at /opt/og/openGauss-server/sse_server_storage)
In this omm shell: isolated.sh gsql | preflight | stop; exit also stops the instance.
~~~

**异常时先停在 B1。** `PREFLIGHT_EXIT=1` 先核对表名和关键词是否已登记，`2` 检查命令参数，`3` 查看 JSON 里的 `path` 和 `reason`，定位缺失、不可读或格式异常的文件。即使显示 `PREFLIGHT_EXIT=0`，后面如果报缺钥、启动失败或返回外层 root，仍未进入可实验的隔离会话。保留原目录和报错，按提示查看诊断文件。

恢复要整套保留原目录中的 `db/instance`、`client` 和 `server`。清文件会丢失恢复所需的状态。`init`、`SSE_init.sql` 和 `seed.sql` 只用于首次初始化。

### B2. 继续原来的实验

进入 **omm 隔离会话** 后，恢复前的检查已经完成。打开本次实例的 gsql：

~~~bash
isolated.sh gsql
~~~

如要做 Fides 关键词查询，在这个 **gsql 会话** 先执行 `fides_setup;`，用原密钥准备本次会话，再继续自己的 SQL。执行完用 `\q` 回到 omm shell。

## 两套流程共用：结束实验并保留目录

在 **omm 隔离会话** 执行：

~~~bash
isolated.sh stop && exit
~~~

预期看到 `ISOLATED_SERVER_STOPPED` 并回到外层 root。个人实验目录会保留，下次继续使用原 `RUN_DIR` 走流程 B。

## 怎么读启动路径和检查结果

每次以启动输出为准。以下是本机一次实测，组员运行时 `RUN_DIR` 会按各自实例变化。

| 字段 | 本机实测或用途 |
| --- | --- |
| `RUN_DIR` | `/var/tmp/fides-isolated.S6pnyc0O`，个人实验目录。 |
| `INSTANCE` | `/var/tmp/fides-isolated.S6pnyc0O/db/instance`，实例数据目录。 |
| `SOCKET`、`PORT` | socket 目录为 `/var/tmp/fides-isolated.S6pnyc0O/socket`，本次连接文件是其中的 `.s.PGSQL.55432`。默认端口为 `55432`，TCP 监听关闭。 |
| `CLIENT` | `/var/tmp/fides-isolated.S6pnyc0O/client`，在会话内绑定到 `/opt/og/openGauss-server/sse_storage`。 |
| `SERVER` | `/var/tmp/fides-isolated.S6pnyc0O/server`，在会话内绑定到 `/opt/og/openGauss-server/sse_server_storage`。 |

入口会核对两侧挂载确实分别指向本次目录。私有挂载只对隔离会话生效，外层仍看到原状态目录。需要另选端口时可用 `bash "$entry/isolated.sh" init --port 55433`，恢复会沿用已保存的端口。

`isolated.sh preflight` 复用只读检查器，读取本次客户端目录里的 `fides_client.mat` 和 `sse_table.mat`，无需连数据库。返回 `0` 表示表和词均已登记，`1` 表示至少一项未登记，`2` 表示参数错误，`3` 表示状态文件缺失、不可读或格式异常。密钥有效性、配对和跨端一致性需另行核对。

## 什么时候才跑排查脚本

日常看到预期结果后即可继续实验。若返回 `1` 或 `2`，先核对表名、关键词和命令。若已有环境返回 `3`、报告缺钥或无法挂载，保留原目录和报错，先核对是否选了正确的 `RUN_DIR`。

输入和路径已确认，仍怀疑检查器结果异常时，去 [handoff.md](handoff.md#何时复跑检查器与验收脚本) 跑离线验证。若需定位隔离或恢复问题，再用交接报告里的联机验收在另建的实例上复现。正式交付验收时也使用这些脚本。

## 放行验收

组长、李孟德、严子涵和张永昌各自在自己的环境执行一次本节。验收脚本会创建新的临时隔离实例，运行公开样例、前置检查、停机和恢复。验收期间，请让外层原状态目录保持无其他写入。

**1. 在外层 root 终端跑联机验收。** 先完成「共用准备」设置 `repo` 和 `entry`，再执行：

~~~bash
release_log=$(mktemp /tmp/fides-release.XXXXXXXX)
cd "$repo" && (set -o pipefail; bash "$entry/preflight/verify-live.sh" 2>&1 | tee "$release_log")
~~~

命令应以退出码 `0` 结束并打印 `PASS LIVE-01` 到 `PASS LIVE-10`。如果有失败，先查看输出的 `EVIDENCE_DIR` 下的日志，保留本次证据。

**2. 在同一个 root 终端核对挂载和摘要。** 下面的命令从本次日志取得路径，可以整块复制。第一组显示客户端和服务端各自的真实目录及挂载目标。第二组只显示比对结论。

~~~bash
evidence_dir=$(sed -n 's/^EVIDENCE_DIR=//p' "$release_log")
run_dir=$(sed -n 's/^RUN_DIR=//p' "$evidence_dir/init.log" | head -n 1)

grep -Fx "CLIENT=$run_dir/client (mounted at /opt/og/openGauss-server/sse_storage)" "$evidence_dir/init.log" &&
  grep -Fx "SERVER=$run_dir/server (mounted at /opt/og/openGauss-server/sse_server_storage)" "$evidence_dir/init.log" &&
  cmp "$run_dir/evidence/preflight.before" "$run_dir/evidence/preflight.after" &&
  echo 'PASS: 隔离状态文件检查前后摘要一致' &&
  cmp "$evidence_dir/outer.before" "$evidence_dir/outer.after" &&
  echo 'PASS: 外层原状态文件摘要一致'
~~~

- **能在本机复跑**。第 1 步退出码为 `0`，临时实例完成启动、检查、停机和恢复。
- **两侧状态确实隔离**。第 2 步应同时显示 `CLIENT=$run_dir/client` 和 `SERVER=$run_dir/server` 对应的两行挂载记录。入口在隔离会话里验证了两个挂载分别指向本次目录，并且不是同一个目录。
- **前置检查前后摘要一致**。第 2 步应显示两条 `PASS`。第一条比对前置检查前后两侧文件的清单、权限、属主和 SHA-256，第二条确认整个联机验收前后外层原状态文件一致。

每人记录自己的放行结果和 `EVIDENCE_DIR`。当前交付附带 [本机实测输出](preflight/result/live.log)，供对照。
