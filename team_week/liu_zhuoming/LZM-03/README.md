# LZM-03｜状态与真实密钥联测

当前契约、修复内容与原生复验结果见 [契约修复与原生复验](契约修复与原生复验.md)。原始交付提交为 `0671a79d068b8c3535b7c511c4aeefe76710a032`，该版本的临时构建通过 18 项模块用例与 15 项真实请求用例，指纹仍以原 [input.json](result/input.json) 为准。

| 文件 | 作用 |
| --- | --- |
| [契约修复与原生复验](契约修复与原生复验.md) | PR #11 审查发现、当前契约以及修复版的复验依据。 |
| [后续修改建议.md](后续修改建议.md) | 原交付的后续事项和当前仍需联测的故障范围。 |
| [handoff.md](handoff.md) | 联测报告、源码修改原因、用例与交接清单。 |
| [env.md](env.md) | 从打开终端开始的一键复跑命令、实际路径和结果判读。 |
| [call-order.md](call-order.md) | 真实请求调用顺序、每个成功/失败分支的标记处理。 |
| [fault-injection.md](fault-injection.md) | 已执行的真实故障注入及手动联调命令。 |
| [preflight/](preflight/) | 10 个可复跑工具。 |
| [result/results.json](result/results.json) | 原始交付的总体结果。 |
| [result/live-cases.json](result/live-cases.json) | 原始交付的 15 项真实请求用例和事件顺序。 |
| [result/contract-native-results.json](result/contract-native-results.json) | 修复版完整原生复验结论、产物指纹以及 18 项真实请求用例。 |
| [result/before-integration/](result/before-integration/) | 请求接入前的缺项和失败证据，保留追溯。 |

本次实际路径为 `gsql SendQuery → execute_request → 状态/真实密钥/Fides → 隔离数据库`。安装的 libpq、程序及系统配置保持原样，原状态目录摘要一致，测试实例已停止。
