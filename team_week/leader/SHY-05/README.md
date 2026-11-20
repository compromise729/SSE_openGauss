# SHY-05｜冻结候选版本并完成独立全套复验

2026-11-20，SHY-05 必测通过，放行 SHY-06。冻结候选为 `main` 的 `5c484c593d3520fa62046871371446d5cd5b7c70`，包含 ZYC-04 PR #17 及其纳入的 YZH-02 交付。运行源码继承 SHY-04 的 `5c8b2b03b482e203ac65a6f989a1b669126f263c`，接口契约为 SHY-02 v1.0。[候选提交](https://github.com/compromise729/SSE_openGauss/commit/5c484c593d3520fa62046871371446d5cd5b7c70) · [ZYC-04 交接](../../zhang_yongchang/ZYC-04/handoff.md)

## 构建与复验结果

在新的 CentOS 7.9 WSL Git 检出上用 GCC 7.3.0 执行 `configure`、`make -j8` 和 `make install`，随后安装同次构建生成的两套 SSE 共享库。使用机器已配置的工具链和 binarylibs，构建目录未引入上一次的对象缓存或安装目录。`gsql`、`gaussdb` 和实库 `SELECT version()` 均报告 `build 5c484c59`。已核对客户端加载的 `libpq_ce.so.5`，并通过运行中的新实例确认 `gaussdb` 及 SSE 共享库来自本次安装。[运行版本、加载路径与六项 SHA-256](evidence/build.json)

| 入口 | 实测结果 |
| --- | --- |
| ZYC 官方入口 | 54/54，包括入口校验 5 项、状态与密码模块 18 项、基线实库 18 项、ZYC 实库 13 项。 |
| YZH-02 | 18/18。同一完整明文的两次封装与 nonce 均不同，认证后还原相同。 |
| LMD-03 | 11/11。覆盖重复 rid、写入与重插失败、缺行、封装边界。必测 FAIL 和意外 SKIP 均使验证失败。 |
| LZM-04 | 36/36。用同一候选和新编对象生成临时故障验证程序，复现真实 SIGKILL、锁竞争、计数保存失败、部分登记与服务端重启。 |
| 独立原生补测 | 普通 SQL、旧 Fides 冒烟、单字符关键词及中文前缀、5 个非整数 rid、v1 物理列、8 种不支持 SQL、实际服务端版本均符合对应预期。 |
| 其他前置复跑 | OpenSSE 89/89，YZH-01 常规模块 226 个断言及随机源故障 8 个断言通过，末行批处理按预期报错。LZM-01 的 19 类夹具共 38 次检查通过，缺参数返回 2。 |

四套汇总共 119 项，退出码 `0`，YZH-02 内部重复运行的 ZYC 用例未重复计入。两组原生补测也退出 `0`。本次通过记录对应的 8 个隔离实例均已停机。[逐用例结果](evidence/matrix-results.json) · [新版补测](evidence/native-row-v1.json) · [旧版补测](evidence/native-legacy.json)

## 一次完整搜索的实测证据

公开夹具从 gsql 用户入口写入两条 apple 和一条 banana。服务端业务表字段为 `rid, data_blob`，索引表字段为 `ut, locator_blob`。请求 05 使用 Fides 令牌命中两个加密位置，认证解封恢复两个 rid。索引重插和 `sse_v1_fetch` 使用同一 rid 集合，回查只返回两条对应业务密文。本地完成认证解密后输出 `1|apple|red apple` 与 `2|apple|green apple`，各一次。banana 一条，健康未知词零条，重复搜索和追加后均按完整行多重集合比对。发送记录保留公开夹具的令牌请求和密文，派生密钥已遮盖。[请求 04/05 的写入、命中、重插、定向回查及输出](evidence/apple-wire.json)

末行标签篡改、定向回查缺行和批量第二行失败分别返回 `auth_failed`、`inconsistent_result` 和 `db_failed`，退出码均为 `3`，业务明文输出为零。各次故障后的新客户端返回 `incomplete_operation` 并退出 `1`。旧 Fides 的未登记词保留 ZYC-01 防护错误，计数、映射及索引均未变化。[验收矩阵与缺口状态](验收矩阵.md) · [待修与未验证项](遗留问题.md)

## 复跑入口

按仓库的 CentOS 7 环境配置准备工具链，使用 `omm` 拥有的全新候选检出。在另一个检出中保留 SHY-05 报告和补测工具。下例从外层 Linux root 终端执行，使用私有挂载命名空间将候选映射到该构建的固定路径。[环境配置](https://github.com/compromise729/SSE_openGauss/blob/5c484c593d3520fa62046871371446d5cd5b7c70/%E7%8E%AF%E5%A2%83%E9%85%8D%E7%BD%AE.md)

```bash
# 新检出先由 omm 检出冻结提交，按实际路径填写下面两项
repo=/opt/og/shy05-candidate/source
tools=/opt/og/shy05-report/team_week/leader/SHY-05
outer_ns=$(readlink /proc/self/ns/mnt)

unshare -m --propagation private bash -s -- "$repo" "$tools" "$outer_ns" <<'SHY05'
set -euo pipefail
repo=$(realpath -e -- "$1")
tools=$(realpath -e -- "$2")
fixed=/opt/og/openGauss-server
test "$(readlink /proc/self/ns/mnt)" != "$3"
test -d "$fixed" && test ! -L "$fixed"
mount --make-rprivate /
mount --bind "$repo" "$fixed"
test "$fixed" -ef "$repo"
(cd "$repo" && test "$(runuser -u omm -- git rev-parse HEAD)" = 5c484c593d3520fa62046871371446d5cd5b7c70)

runuser -u omm -- bash -e -s -- "$fixed" <<'BUILD'
source /home/omm/.sse_opengauss_env
cd "$1"
export CODE_BASE="$PWD" GAUSSHOME="$PWD/dest"
./configure --gcc-version=7.3.0 CC="$CXX" CFLAGS='-O0' \
  --prefix="$GAUSSHOME" --3rd="$BINARYLIBS" --enable-debug --enable-cassert \
  --enable-thread-safety --without-readline --without-zlib
make -j8
make install
install -m 755 sse/opensse-schemes/third_party/crypto/build/src/libsse_crypto.so "$GAUSSHOME/lib/"
install -m 755 sse/Mitra/third_party/crypto/library/lib/libmitra_sse_crypto.so "$GAUSSHOME/lib/"
BUILD

evidence=$(mktemp -d /var/tmp/shy05-evidence.XXXXXXXX)
printf 'EVIDENCE_DIR=%s\n' "$evidence"
env -u MANPATH bash "$repo/team_week/zhang_yongchang/ZYC-04/reproduce.sh" \
  --repo "$repo" --evidence-dir "$evidence/aggregate"
bash "$tools/verify-native.sh" "$repo" "$evidence/native"

runuser -u omm -- bash -e -s -- "$fixed" <<'MODULES'
source /home/omm/.sse_opengauss_env
cd "$1/sse/opensse-schemes/third_party/crypto/build"
ctest --output-on-failure -j4
cd "$1/team_week/yan_zihan/YZH-01"
openssl_dir="$BINARYLIBS/dependency/centos7.6_x86_64/openssl/comm"
export OPENSSL_INCLUDE_DIR="$openssl_dir/include" OPENSSL_LIB_DIR="$openssl_dir/lib"
export LD_LIBRARY_PATH="$1/dest/lib:$OPENSSL_LIB_DIR:$LD_LIBRARY_PATH"
bash build.sh
bash run.sh
MODULES
bash "$repo/team_week/liu_zhuoming/LZM-01/preflight/verify-offline.sh"
SHY05
```

预期出现 `ZYC04_REPRODUCE_EXIT=0`、`SHY05_NATIVE_EXIT=0` 和 `ISOLATED_SERVER_STOPPED`。在本交付目录执行 `sha256sum --strict -c manifest-sha256.txt` 核验文件。SHY-06 接续时检出本页冻结提交，并记录实际使用的候选和产物指纹。
