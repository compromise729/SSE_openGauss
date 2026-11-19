# 团队任务交付

先在仓库中执行 `git fetch origin`，再从最新 `main` 查看任务交接和验证记录。开发任务按交接报告中的固定提交开始，新增文档通过个人分支和 PR 更新。报告记录对应任务、完整源码提交号、复跑命令和实测结果。

| 负责人 | 任务入口 |
| --- | --- |
| 施骅洋 | [SHY-01 基线](leader/SHY-01/README.md) · [SHY-02 接口契约](leader/SHY-02/README.md) · [SHY-03 发布与接续](leader/SHY-03/README.md) · [SHY-04 联合测试基线](leader/SHY-04/README.md) |
| 张永昌 | [ZYC-01 客户端防护](zhang_yongchang/ZYC-01/README.md) · [ZYC-02 密文写入](zhang_yongchang/ZYC-02/insert-report.md) · [ZYC-03 检索与本地展示](zhang_yongchang/ZYC-03/search-report.md) · [完整复验入口](zhang_yongchang/README-ZYC-02-03.md) |
| 严子涵 | [YZH-01 随机化认证加密](yan%20zihan/YZH-01/handoff.md) |
| 李孟德 | [LMD-01 密文数据库接口](li_mengde/LMD-01/handoff.md) · [LMD-02 真实密文往返](li_mengde/LMD-02/handoff.md) |
| 刘卓明 | [LZM-01 隔离环境](liu_zhuoming/LZM-01/README.md) · [LZM-02 状态与标记](liu_zhuoming/LZM-02/handoff.md) · [LZM-03 状态与密钥联测](liu_zhuoming/LZM-03/README.md) |

在 Git 中使用任务目录中的当前工具。同名而内容不同的 Windows 历史工具存放于对应任务的 `windows-original/`。核对原交付清单时，按归档记录定位原文件。Windows 上的严子涵目录名为 `yan zihan`，Git 使用 `yan_zihan`。密钥、私有实验状态和可由源码重建的二进制保存在各自环境。[Windows 交付归档记录](leader/SHY-03/Windows交付归档.json)
