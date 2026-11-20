# YZH-02｜真实客户端认证与错误传播

严子涵交付，组长在独立 CentOS 7 实例复跑。使用 SHY-04 固定源码 `5c8b2b03b482e203ac65a6f989a1b669126f263c`、契约 v1。Git 分支包含复跑工具、验收矩阵和公开实例证据。

## 复跑

在已按 [SHY-04](../../leader/SHY-04/README.md) 配置和原生安装的独立副本检出包含本任务工具的提交。保留同批 `gsql`、`gaussdb` 和 `libpq_ce`。构建配置依赖固定绝对路径时，在私有挂载命名空间中将独立副本映射到该路径。进入项目根目录，从 CentOS 7 外层 root 终端执行：

```bash
repo=$(pwd -P)
bash "$repo/team_week/zhang_yongchang/tools/zyc_live_verify_all.sh" --project-root "$repo"
OPENGAUSS_PROJECT_ROOT="$repo" bash "$repo/team_week/liu_zhuoming/LZM-01/isolated.sh" init --port 55444 \
  < "$repo/team_week/yan_zihan/YZH-02/yzh02_session.sh"
```

第二条入口核对实际检出版本及固定源码，使用项目内已安装的真实客户端，先运行 13 项 ZYC 实库夹具，再执行 YZH-02 的 18 项。公开数据证据位于输出的 `RUN_DIR/evidence/yzh02`，尾行认证失败证据位于同一次执行的 `RUN_DIR/evidence/live`。预期输出 `YZH02_RESULT=PASS CASES=18` 和 `ISOLATED_SERVER_STOPPED`。

## 核对结果与交接

- [故障矩阵](fault-matrix.md) 对应任务书中的随机化、文本边界、业务与位置认证、错钥和错误传播。
- [复验摘要](results/review.json)、[逐项结果](results/results.json) 和 [末行失败证据](results/last-row-authentication.json) 记录本次被测代码与退出状态。
- [完整行随机化](results/public-duplicate-example.json) 和 [位置认证](results/public-locator-example.json) 使用公开合成夹具。重复完整行的两份密文与 nonce 均不同，任一业务行认证失败时整次查询无业务输出。
- 前置 [SHY-04 实库与构建记录](../../leader/SHY-04/README.md) 和 [YZH-01 模块验证](../YZH-01/handoff.md) 记录长载荷、尾零字节和随机源故障等已有覆盖。

交接给组长和张永昌 ZYC-04。本次交付为测试和证据。协议的同表有效位置错配、回放与遗漏检测沿用 SHY-04 约定的范围。
