# LZM-01｜preflight 验收用例

本文件供排查和交付验收。日常开发按 [env.md](../env.md) 做一次前置检查即可。复跑条件与入口命令见 [handoff.md](../handoff.md#何时复跑检查器与验收脚本)。

## 离线

[verify-offline.sh](verify-offline.sh) 调用 [generate_fides_fixtures.py](generate_fides_fixtures.py) 生成公开夹具，并以 `omm` 运行 [verify_cases.py](verify_cases.py)。每个 T2 用例执行两次，OFF-01 单独检查参数错误。实测见 [逐项终端输出](result/offline.log) 和 [检查器 JSONL](result/offline.jsonl)。

| 用例 | 输入或前提 | 预期结果 |
| --- | --- | --- |
| T2-01 | 正常夹具 | 两个目标已登记 |
| T2-02 | 计数和映射均为空文件 | 两个目标未登记 |
| T2-03 | 正常夹具，词 `unlisted` | 仅词未登记 |
| T2-04 | 正常夹具，表 `unlisted` | 仅表未登记 |
| T2-05 | 词 `green apple` | 保留词中的空格 |
| T2-06 | 非数字计数 | 报告格式错误 |
| T2-07 | 计数记录截断 | 报告格式错误 |
| T2-08 | 缺少末尾计数 | 报告格式错误 |
| T2-09 | 计数超出整数上限 | 拒绝越界值 |
| T2-10 | 计数为负数 | 拒绝负数 |
| T2-11 | 已登记词计数为 0 | 返回实际计数 0 |
| T2-12 | 已登记词处于整数上限 | 接受上限 |
| T2-13 | 缺少计数文件 | 报告计数文件缺失 |
| T2-14 | 缺少映射文件 | 报告映射文件缺失 |
| T2-15 | 两份文件均无读权限，以 `omm` 检查 | 报告不可读 |
| T2-16 | 映射缺少有效列项 | 报告格式错误 |
| T2-17 | 映射使用未定义方案 | 报告格式错误 |
| T2-18 | 表有重复映射 | 返回最后登记的该表映射 |
| T2-19 | 计数含无效编码 | 报告编码错误 |
| OFF-01 | 缺少 `--keyword` | 参数错误，诊断在 stderr |

## 联机

[verify-live.sh](verify-live.sh) 另建隔离实例，[seed.sql](seed.sql) 提供公开数据，[resume.sql](resume.sql) 验证原目录恢复。实测见 [逐项终端输出](result/live.log) 和 [检查器 JSONL](result/live-preflight.jsonl)。

| 用例 | 命令或输入 | 预期结果 |
| --- | --- | --- |
| LIVE-01 | `isolated.sh init`，初始化前两侧为空 | 前置检查返回 3，随后启动独立实例 |
| LIVE-02 | 导入 `seed.sql` 后 preflight 已登记表和 apple 关键词 | 返回 0，计数 2 |
| LIVE-03 | preflight 使用同一表和未登记词 | 返回 1 |
| LIVE-04 | preflight 仅传 `--table team_fides_demo` | 返回 2，诊断在 stderr |
| LIVE-05 | 对两侧状态做检查前后快照，停机后比较外层快照 | 两侧独立挂载，两组文件摘要一致 |
| LIVE-06 | `stop` 后用同一 `RUN_DIR` 执行 `resume`，再用 `resume.sql` 查询 | 原状态仍可用，apple 查询返回 1、2 各一次 |
| LIVE-07 | 在缺计数文件的停机副本上执行 `resume` | 返回 3，拒绝启动，保留现场 |
| LIVE-08 | 在计数文件被截断的停机副本上执行 `resume` | 返回 3，拒绝启动，保留现场 |
| LIVE-09 | 在缺少客户端私钥的停机副本上执行 `resume` | 前置检查仍可返回 0，恢复入口拒绝启动 |
| LIVE-10 | 恢复后令隔离会话以 29 退出 | 原样返回退出码并停机 |

## 输出保存方式

`result/` 保存已执行的验收输出，`.log` 是本次终端记录，`.jsonl` 每行是一条检查结果。联机 JSONL 只整理含公开表名、关键词和检查状态的记录。参数诊断、摘要比对结果及异常副本的验证结论在对应的 `.log` 中。原始快照和密钥摘要保留在本机私有证据目录。

用例里的 `PASS` 表示实测符合预期。例如应报文件异常的用例收到退出码 `3`，该用例通过。
