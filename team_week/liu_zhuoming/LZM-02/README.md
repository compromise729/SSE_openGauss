# LZM-02｜交付入口

完整交付内容集中在 [handoff.md](handoff.md)，按任务要求、文件作用、实现方法、用例结果、放行条件和接续顺序组织。

| 文档或目录 | 用途 |
| --- | --- |
| [handoff.md](handoff.md) | 完整交付文档，含 20 个 Git 文件的清单、改动位置及飞书材料清单。 |
| [state-api.md](state-api.md) | 接口输入输出、调用条件及持久化语义。 |
| [env.md](env.md) | 隔离复跑及新建、恢复说明。 |
| [preflight/](preflight/) | 检查器、可执行夹具和验证脚本。 |
| [result/](result/) | 实测日志及被测源码、构建产物的输入指纹，作为飞书附件交付。 |

目标任务分支为 `feature/LZM-02-state`，PR 目标为 `integration/SHY-03`。Git 提交按 handoff 的 20 个文件逐项选择，报告和日志交飞书。正式输出提交号为 `df1a7cc22dc03305aa9aaa463e210c2fb82178fb`，提交中的 20 个文件已与本地交付清单核对一致。
