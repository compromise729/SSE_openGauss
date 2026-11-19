# YZH-01 交接记录

前置 SHY-02 已接收，契约版本 v1.0。发布标识 b6159cf8d0dd6d3d8befa8fd84816d289a7b88d1 来自交付包的发布版本.json 与 evidence/release-commit.txt，其本地 Git 对象未随包提供。本次以已校验的 SHY-02 文件包和源码基准 25489f563f25b522d0cd37a6c85865039e3f340f 实作。

模块提交为 7b99d773e308ec3c06b71654e517f9abfa5e2ccd，隔离仓库为 /opt/og/yzh01-release.WUApfiBz/source，分支 codex/yzh-01-crypto。原机器上已有 Git 作者为占位符，因此隔离仓库提交使用本地 Codex 作者。综合交付补丁 YZH-01.patch 可在包含 25489f5 的独立分支上，用 git am --3way 应用。交付文件校验值见 manifest-sha256.json。

## 团队 Git 交接

SHY-02 在 `main` 上的正式合入提交为 `7bace5b2182552f1e2fe5865ac66d58812435510`。YZH-01 发布源码提交为 `3d28b327706c65bd8b73f2f79cfbdaf1bfb50949`，通过 [PR #5](https://github.com/compromise729/SSE_openGauss/pull/5) 合入 `integration/SHY-03`，合入提交为 `e986295ccbd950a19a54a46fb4f4ed4a7f68c8b8`。LZM-03 接续 `c3fa234db4531e8de1aca2314e40a9ea0dec828e`。上文的本地交付包标识用于追溯原始证据。

## 文件与接入

- include/sse_v1_contract.hpp 原样保留 SHY-02 接口，include/row_aead_v1.h 是调用入口。src/row_aead_v1.cpp 实作封装、行编解码、rid、密钥与加解密。
- build.sh 构建，run.sh 运行，verify_delivery.sh 记录编译、动态库和测试结果。tests 中的密钥均为公开合成夹具，运行时存放于新临时目录。完整预期、实测和原始日志见 [crypto-report.md](crypto-report.md)。
- verify_storage_fixture.sh 把正式模块生成的公开夹具密文送入独立 openGauss 实例，验证数据库存取、模拟位置替换及客户端回读认证。
- legacy-short-key.patch 仅供旧辅助函数回归，验证入口为 verify_legacy_short_key.sh。现有 sse_common.cpp、libpq/gsql 全局构建规则、SQL 和客户端调用方由各负责人维护。

## 复现编译与运行

在 Linux/WSL 从交付目录运行。使用团队 Git 检出的模块时，将 `YZH01_VECTORS` 指向随验证交接提供的公开 `public-vectors.tsv`，本地交付包可使用自带向量。系统提供匹配的 OpenSSL 开发包和运行库时可直接用 bash verify_delivery.sh。使用 SHY-02 的 GCC 7.3 与项目 OpenSSL 时，先按自己的安装位置设置下列变量。本机实际配置如下。

~~~bash
export CXX=/opt/og/binarylibs/buildtools/centos7.6_x86_64/gcc7.3/gcc/bin/g++
export CXXFLAGS=-D_GLIBCXX_USE_CXX11_ABI=0
export OPENSSL_INCLUDE_DIR=/opt/og/binarylibs/dependency/centos7.6_x86_64/openssl/comm/include
export OPENSSL_LIB_DIR=/opt/og/SSE_openGauss/dest/lib
export LD_LIBRARY_PATH="$OPENSSL_LIB_DIR:/opt/og/binarylibs/buildtools/centos7.6_x86_64/gcc7.3/gcc/lib64"
bash verify_delivery.sh
bash verify_legacy_short_key.sh /opt/og/yzh01-release.WUApfiBz/source
~~~

调用方用与库相同的编译器 ABI，并使用对应 OpenSSL 头文件和运行库。若出现 libcrypto 未找到或 GLIBCXX 符号错误，先检查 ldd 和动态库搜索路径。库文件 build/librow_aead_v1.so 由脚本生成，与测试程序同目录，源码提交不携带各机器的编译产物。

存取夹具需要已安装的 openGauss、可创建 mount namespace 的 Linux root，以及专用的 SQL 运行用户（默认 omm）。脚本在私有挂载命名空间中绑定临时 SSE 状态目录，只通过新建的 Unix socket 连接临时实例。按本机安装设置以下路径后在交付目录运行。

~~~bash
export GAUSSHOME=/opt/og/SSE_openGauss/dest
export SSE_LEGACY_ROOT=/opt/og/openGauss-server
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/opt/og/SSE_openGauss/sse/opensse-schemes/third_party/crypto/build/src:/opt/og/SSE_openGauss/sse/Mitra/third_party/crypto/library/lib:/opt/og/fides_relic/lib:/opt/og/mitra_relic/lib"
bash verify_storage_fixture.sh
~~~

## 接手

- **李孟德** 用实产 Base64 验证 SQL 存取、数组处理与重插；行长度 88～45144，协议结构上限为 65624，位置严格 96 字符。
- **刘卓明** 显式创建新状态时，由 LZM-02 持锁，在私有空目录调用 YZH-01 密钥初始化，核对后发布到状态目录。已有或未知状态使用 load_keys，缺钥即终止。将目录范围和表 Scope 与实际连接匹配。
- **张永昌** 写入前先通过 new_rid、encrypt_row 和 encrypt_locator，认证失败立即停止。回查业务行时以预期 rid 解密，全部成功后才能统一展示。用新版真实 gsql 复验篡改最后一行与批处理非零退出。
- **组长** 在 SHY-03 纳入统一构建。本轮按诚实但好奇模型验收，服务端维护 ut 和位置密文的对应。若后续要求检测同表位置错配，由组长发布纳入规范 ut 的新版位置 AAD。
