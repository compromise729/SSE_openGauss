# SHY-01 成果入口

先读 [统一基线](统一源码与运行基线.md)，再读 [成员补丁清单](成员成果与补丁清单.md) 和 [交接记录](交接记录.md)。

[验收结果](results.json) 由 verify_delivery.py 对原始日志的完整行、计数、构建及环境保持结果核验生成。[文件摘要](manifest-sha256.json) 用于核对本轮交付。

源码与安装副本位于 统一源码与运行基线.md 记载的 WSL 路径，数据库已停止。prepare.sh、audit.sh、build-smoke.sh、final-check.sh 和 sql/ 提供复现入口，evidence/ 保留实际证据。
