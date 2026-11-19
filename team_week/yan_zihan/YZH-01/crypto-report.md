# YZH-01 业务行与位置认证加密

## 输入与交付

接收 SHY-02 v1.0，发布标识为 b6159cf8d0dd6d3d8befa8fd84816d289a7b88d1，源码基准为 25489f563f25b522d0cd37a6c85865039e3f340f。SHY-02 文件包的 33 项 SHA-256 全部匹配，见 [前置复验日志](results/shy-02-check.log)。原样附入的 sse_v1_contract.hpp 的 SHA-256 为 1d40431b825d6b79706efd38220b6fd1809157eecb4c80e1b884191ad4d73371。模块提交号及获取方法见 [交接记录](handoff.md)。

模块的入口为 include/row_aead_v1.h。src/row_aead_v1.cpp 实现 SHY-02 分配给 YZH-01 的九个函数。build.sh 生成可链接的 build/librow_aead_v1.so，验收程序直接调用这个动态库。使用 OpenSSL 的 EVP AES-256-GCM 和 RAND_bytes，检查随机源、每步 EVP 调用和实际输出长度。解密的中间缓冲区先保留在模块内部，只有认证通过后才解析业务行或位置。所有入口将输出清空，只有成功才返回结果。合法空业务字段和失败通过 Status 区分。参考 [OpenSSL EVP](https://docs.openssl.org/1.0.2/man3/EVP_EncryptInit/) 和 [RAND_bytes](https://docs.openssl.org/1.0.2/man3/RAND_bytes/)。

## 数据与长度

- 业务行依次编码 ind、keyword、payload，每项是 4 字节大端长度和原始字节。UTF-8 文本按字节保留中文、空格和空串。序列化层也保存 NUL，SQL 文本的 NUL、CR、LF 拒绝由客户端入口完成。
- rid 是独立的 16 字节随机记录标识，在线路上用 32 位小写十六进制表示。位置明文固定为 16 字节 rid 加 1 字节操作值，v1 只接受 0。空业务字段合法，空 rid 非法。
- 封装为标准带填充 Base64。二进制按顺序为 SSE1（4）、版本（1）、用途（1）、key_id（16）、nonce（12）、大端密文长度（4）、密文和标签（16），括号内单位为字节。解密检查严格 Base64、长度、版本、用途及 key_id。密文长度和原始明文长度相同。
- AAD 是整个 38 字节头，加长度前缀编码的 deployment_id、database_oid、schema、table。业务行还追加已知 rid 的原始 16 字节。位置按 v1 使用 Scope；rid 与 op 一起加密和认证。

| 数据 | 明文长度 | 二进制封装 | Base64 |
|---|---:|---:|---:|
| 三字段均为空 | 12 | 66 | 88 |
| 三字段达到允许上限（16384 + 1024 + 16384） | 33804 | 33858 | 45144 |
| SHY-02 结构上限（按三个 16384 字段计算） | 49164 | 49218 | 65624 |
| 位置 | 17 | 71 | 96 |

二进制长度为明文长度加 54，Base64 长度为 4 × ⌈二进制长度 / 3⌉。结构上限用于封装预检和物理表约束。实际行必须同时遵守 1024 字节的关键词上限，因而可生成的最大业务封装为 45144 字符。解码还会拒绝声明长度超限、截断或尾随字节的行。

## 密钥与状态

客户端以独立的 32 字节 Krow、32 字节 Kloc 和 16 字节 key_id 保存到 row_aead_v1.key、locator_aead_v1.key、row_aead_v1.keyid。仅在上游明确判断全新环境后，先建立归当前用户所有的空 0700 目录，再调用 initialize_keys。调用方应在其他状态文件写入前完成此步骤。三个文件独占创建，权限 0600，key_id 最后写入，写入和目录同步均检查。中途出错时保留现场，缺项或非空状态继续阻断。目录锁避免同时初始化和读取半套密钥。

load_keys 只读。已有状态缺任一文件、短钥、过长文件、权限或属主不符合要求、符号链接和两用途钥相同，均返回 key_invalid。封装 key_id 不匹配也返回 key_invalid。同 key_id 的错钥以及修改 nonce、密文或标签返回 auth_failed。随机源失败返回 crypto_failed。原有 join_col 辅助路径和 CBC 格式不参与新协议。

## 实测记录

主环境是 WSL CentOS 7、GCC 7.3.0、C++11、旧 libstdc++ ABI、项目 dest/lib 中的 libcrypto.so.1.1。以上构建和三组测试均从模块提交导出的干净副本复跑，路径包含空格。测试使用公开夹具和临时目录，没有使用现存业务密钥。

| 用例 | 命令及输入 | 预期和实测 | 原始日志 |
|---|---|---|---|
| CRYPTO-01 | bash verify_delivery.sh。SHY-02 三条公开样例，每行加密两次 | 三行逐字段、逐字节等长恢复。两次 nonce 和密文各自不同 | [module.log](results/module.log) |
| CRYPTO-02 | 同入口。中文、空格、空字段、尾零、15/16/17 与 63/64/65 字节、字段最大和超长 | 精确往返。空行 88 字符，最大合法行 45144 字符。越界清空输出并拒绝 | [module.log](results/module.log) |
| CRYPTO-03 | 同入口。位置 op=0、重新加密、错 Scope/rid/key_id/密钥、非法版本和用途、修改 nonce/密文/标签、截断、非法 Base64，以及有合法标签但内容违法的公开向量 | 合法位置恢复；错误返回且没有行或位置输出。有合法标签的非法操作值或行长度返回 protocol_invalid | [module.log](results/module.log) |
| KEY | 同入口。缺钥、空钥、16/31/33 字节钥、短 key_id、权限、链接、互斥及二次初始化 | 缺钥与短钥拒绝，不补钥，原钥保留。只在显式初始化时创建 | [module.log](results/module.log) |
| RNG | 同入口。仅测试用 LD_PRELOAD 将 RAND_bytes 返回值置为失败 | 行、位置、rid 和钥生成均返回 crypto_failed。没有输出和新钥文件 | [module.log](results/module.log) |
| 批结果夹具 | 同入口。第三行标签损坏，独立驱动先暂存前两行 | 驱动 stderr 为 auth_failed，退出 17，stdout 为 0 字节 | [module.log](results/module.log) |
| openGauss 存取夹具 | bash verify_storage_fixture.sh。独立实例、SHY-02 三条公开行、六个实际封装、一次位置重新加密并替换 | 真实 gsql 收到的列值为 rid/ut 和密文。读回三个业务封装和三个位置封装，密文字节与提交前相等。客户端认证恢复业务行、rid、op。实例正常停止 | [storage-fixture.log](results/storage-fixture.log) |
| 旧短钥 | bash verify_legacy_short_key.sh <含 25489f5 的 Git 仓库>。读取旧路径的 0、15、16 字节样本 | 在隔离源码副本中，旧基准把前两例当作 16 字节，旧补丁拒绝前两例并保留 16 字节正常行为。新模块另测 32 字节文件 | [legacy-short-key.log](results/legacy-short-key.log) |

旧补丁源自本机 team_prep 的 short_key_check.patch。其原文件、a3567e4 与 SHY-02 基准 25489f5 的 sse_common.cpp blob 均为 18b3add01e6408d19ff92108f435d423758a49c8。legacy-short-key.patch 是按仓库相对路径整理的回归输入，未应用到本交付的生产源码。

另用系统 libcrypto.so.10 复验了独立模块。未配置运行库时，加载器报告系统 libstdc++ 缺少 GLIBCXX_3.4.20/21，测试进程尚未启动。补上本机匹配的 GCC 7.3 运行库路径后，同一组测试通过。证据见 [加载失败日志](results/system-runtime-mismatch.log) 与 [复跑日志](results/system-openssl.log)。

公开向量在 [public-vectors.tsv](tests/public-vectors.tsv)。其中两条行向量使用相同的完整明文和不同的固定测试 nonce；另有一条位置向量和两条合法认证、非法内容的负例。生成脚本使用 Python cryptography；生产模块只用 RAND_bytes，未提供固定 nonce 入口。

## 现有契约的边界及待集成验证

业务行绑定 rid，跨记录替换实测返回 auth_failed。位置载荷绑定 Scope，跨部署、数据库或表替换实测被拒绝。**同一 Scope 中，把一条合法完整 locator_blob 换到另一个 ut 下，v1 密码接口仍能认证。** 公开测试换入另一条有效位置并恢复了另一 rid，表明位置 AAD 没有绑定 ut 或外部预期 rid。检查返回的 ut 是本次请求成员，无法独立证明对应 locator_blob 原来存在哪个 ut 下。若集成验收必须拒绝这类替换，需要由组长发布包含可信预期定位标识的契约版本，涉及 YZH、ZYC 与 LMD。

真实 Fides 的 INSERT、索引搜索、客户端重插、按 rid 回查，以及最后一行损坏时 gsql 的 ON_ERROR_STOP 行为，需在 SHY-03、LMD 与 ZYC 接入后复验。openGauss 存取夹具用了真实数据库和 SQL 物理形状，ut 为公开占位，调用链尚未接入 Fides。夹具通过 Unix socket 使用隔离实例，未启用 TCP。旧原生列加密路径的组合 DDL 和篡改退出码问题仍在。

Scope 认证不能代替 SSE 计数命名空间隔离，现阶段仍限隔离环境和单客户端。密钥轮换、备份恢复、丢钥恢复、旧数据迁移、多关键词、范围查询、服务端密文 JOIN、TCP/TLS、整链性能和形式化安全证明均未交付。
