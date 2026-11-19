# SHY-02 成果入口

本任务对应全体截止清单第三项，冻结接口、数据格式和验收样例。基线为 SHY-01 的 25489f5，前置环境采用已交付的 LZM-01。

- [接口与数据格式约定](接口与数据格式约定.md) 固定用户入口、字节封装、随机化认证加密、SQL 签名、状态和错误约定。
- [验收样例与预期结果](验收样例与预期结果.md) 为四人提供公开样例和模块/链路验收条件。
- [四人接续实现清单](四人接续实现清单.md) 明确代码归属、接收顺序和构建纳入位置。
- [交接记录](交接记录.md) 记录发布提交、实测证据与成员接收状态。
- include/sse_v1_contract.hpp 是可编译共用声明，sql/ 是可执行物理形状草案，checks/ 是头文件与密码库可用性检查。

复验入口为 verify.sh，Windows 中执行 WSL 命令，随后运行 verify_delivery.py 核对交付日志。

```powershell
wsl -d CentOS7 -u root -- bash '/mnt/c/Study/创新训练计划/team_week/leader/SHY-02/verify.sh'
python team_week/leader/SHY-02/verify_delivery.py
```

结果保存在 evidence/。verify.sh 使用 SHY-01 独立安装和新建临时实例，退出时停止服务。重跑会更新当前检查证据，需要保留旧证据时先复制整份交付目录。
