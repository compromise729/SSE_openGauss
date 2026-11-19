# LZM-01｜隔离环境与 Fides 前置检查

本任务让组员能启动自己的 Fides 实例，在实验前只读检查状态，停机后用同一套数据库和两侧状态继续实验。

| 任务要求 | 本次完成内容 |
| --- | --- |
| 修复目录定位 | 旧复跑脚本从自身位置定位项目根目录。交付入口分别定位项目根目录、工具文件夹和个人实验目录。启动时打印实例、socket、端口及客户端和服务端状态路径。 |
| 运行只读前置检查 | 在隔离会话里使用 plan2 的检查器，保留 JSON 和 `0/1/2/3` 退出码。实测前后两侧状态文件摘要一致。 |
| 新建与恢复 | `init` 创建独立实验，`stop` 停机并保留状态，`resume` 复用原数据库和两侧状态。 |

按操作手册复跑，按任务报告核对每个用例的输入、预期、实测和证据。

| 文件 | 用途 |
| --- | --- |
| [env.md](env.md) | 日常操作手册。第一次走初始化流程，后续按原目录恢复，两套流程都包含真正实验前的前置检查。 |
| [handoff.md](handoff.md) | 任务报告。说明什么时候才复跑验证脚本，并记录用例、输出及隔离和恢复的结果。 |
| [preflight/cases.md](preflight/cases.md) | 用例编号、输入、预期结果及对应的可执行脚本。 |
| [preflight/result/](preflight/result/) | 本次实测的离线、联机终端输出和检查器 JSONL。 |
| [isolated.sh](isolated.sh) | 隔离环境的入口，提供 `init`、`resume`，会话内提供 `preflight`、`gsql`、`stop`。 |
| [preflight/check_fides_state.py](preflight/check_fides_state.py) | 只读检查客户端的关键词计数和表映射，沿用 plan2 实现。 |
| [preflight/generate_fides_fixtures.py](preflight/generate_fides_fixtures.py) | 生成 plan2 的公开离线夹具。 |
| [preflight/verify_cases.py](preflight/verify_cases.py) | 逐个执行并比对离线用例，检查器从同一工具文件夹加载。 |
| [preflight/verify-offline.sh](preflight/verify-offline.sh) | 跑公开夹具和参数错误用例，比较夹具前后快照。 |
| [preflight/verify-live.sh](preflight/verify-live.sh) | 跑隔离实例的联机检查、摘要比对、停机和恢复。 |
| [preflight/seed.sql](preflight/seed.sql) | 新建实例时导入初始化 SQL 并写入公开样例。 |
| [preflight/resume.sql](preflight/resume.sql) | 恢复后在同一会话运行 Fides setup 和公开查询。 |

放行时，每位组员都要在自己的环境复跑，确认两侧状态目录实际隔离，检查前后文件摘要一致。步骤见 [env.md 的放行验收](env.md#放行验收)。
