# 新版密文表与服务端接口

## 物理结构

逻辑 schema 固定为 `public`，逻辑表名匹配 `[a-z][a-z0-9_]{0,39}`。下表列出 `week_demo` 对应的数据库对象。

| 对象 | 字段 | 用途 |
|---|---|---|
| `public.week_demo_data` | `rid text PRIMARY KEY`, `data_blob text NOT NULL` | 客户端随机生成的 32 位小写十六进制 rid，定位一条整行密文 |
| `public.week_demo_index` | `ut text PRIMARY KEY`, `locator_blob text NOT NULL` | Fides 更新令牌指向位置封装，客户端可认证恢复 rid 与操作值 |
| `public.sse_v1_catalog` | `logical_schema, logical_table, protocol_version` | 逻辑表与服务端协议版本 1，schema 和表名组成主键 |

索引表只保存两列。新表没有旧 `en_ind`、`join_col` 或业务字段列。服务端检查 Base64、`SSE1`、版本、用途、封装声明长度及标签所在空间，认证由持有密钥的客户端完成。位置封装固定 96 字节。业务封装物理上限 65,624 字节，在 YZH-01 关键词上限下实际最大为 45,144 字节。

## 调用约定

| 调用（均在 `public`） | 返回和事务 |
|---|---|
| `sse_v1_create(text,text)` | `void`。主表、索引和 catalog 登记同一个数据库事务。物理对象已占用或标识非法时抛 SQL ERROR。 |
| `sse_v1_insert(text,text,text[],text[],text[],text[])` | `void`。依次传 schema、逻辑表、rids、data_blobs、uts、locator_blobs。数组等长，1 至 100 行。主表和索引一批整体写入，任一行或约束失败时整批回滚。 |
| `sse_v1_reinsert(text,text,text[],text[])` | `void`。传 uts、locator_blobs 两个等长数组，只写索引。空数组为成功的空操作。 |
| `sse_v1_fetch(text,text,text[])` | `TABLE(rid text, data_blob text)`。按完整 rid 连接主键。空数组返回零行。集合缺失或请求 rid 无效、重复时抛 SQLSTATE `21000`。 |

客户端使用 `PQexecParams`。每个数组作为一个参数传给 `$n::text[]`，数组元素用引号包裹并转义反斜杠和双引号。变动标识在服务端验证后经 `format('%I', ...)` 引用，值通过 `EXECUTE ... USING` 绑定。`PQgetlength` 读取实际封装长度，调用方按完整字符串验证字节和集合，避免固定长度缓冲区。可复制的调用代码见 [真实往返驱动](../LMD-02/fides-roundtrip-probe.cpp)。

使用以下 SQL 读取服务端登记。建表成功并提交以后，客户端再写本地版本和表映射。建表失败返回 SQL ERROR，客户端的操作标记和本地登记由客户端状态接口处理。

```sql
SELECT protocol_version
FROM public.sse_v1_catalog
WHERE logical_schema = 'public' AND logical_table = 'week_demo';
```

SQLSTATE `23505` 是重复键，`23514` 是封装或标识的物理约束失败，`22023` 是参数或线路格式非法，`42P01` 是新版目标缺失、未登记或形状不符，`42P07` 是建表名称已占用。回查的 `21000` 映射到 `inconsistent_result`，其它服务端错误按冻结契约作为该轮数据库操作失败处理。

## Fides 索引路径

同一连接完成 `fides_setup;` 后，用 `PQexec` 发送客户端生成的内部搜索请求。将占位符替换为实际 Fides 请求字段。

```text
fides_v1_select * from public.week_demo_index where token=<Base64> and derivation_key=<Base64> and add_count=<十进制计数>;
```

- 服务端使用现有 Sophos 逻辑推导更新令牌，按 `ut` 匹配索引并取出记录，返回 `ut, locator_blob`。该轮取出采用 `DELETE ... RETURNING`，已消费的 ut 再查返回零行。
- 按 ut 校验本次命中的位置，认证并恢复 rid 后，客户端生成新 ut 和新位置封装，再用 `sse_v1_reinsert` 写回。之后仅用认证过的 rid 调用 `sse_v1_fetch`。
- 零计数、空 token 和空 derivation_key 是空请求，适配层验证目标后返回两列零行。健康状态下未登记关键词由客户端在发请求前处理。
- 以集合处理命中结果，尤其保留完整 rid 与业务行的对应关系。索引取出、重插、回查分别有数据库调用边界。跨调用中断用冻结契约的客户端未完成标记处理。

## 构建与复跑

在团队的隔离源码分支及 openGauss 构建环境中执行。`deliverables` 指向本 `li_mengde` 目录，`lzm01` 指向团队 LZM-01 包。有任务分支时检出对应源码。补丁交接时，组长在已确认的基准分支上应用下列补丁，补丁包含 `sse_sql/SSE_v1.sql` 和配套工具。构建与安装走 SHY-01 的流程。已有同基准完整安装时，本次增量构建更新了 `dest/bin/gaussdb`。

```bash
git -c core.whitespace=cr-at-eol am --keep-cr "$deliverables/LMD-01/LMD-01-storage.patch"
make -j8
install -m 755 src/gausskernel/gaussdb dest/bin/gaussdb
```

在 root 的隔离构建环境中启动 LZM-01。`OPENGAUSS_PROJECT_ROOT` 指向新构建的源码安装目录。

```bash
OPENGAUSS_PROJECT_ROOT="$project" bash "$lzm01/isolated.sh" init --port 55462
```

在打开的 `omm` 会话内，将 `deliverables` 设为同一交付目录的绝对路径，加载旧版基础函数后执行本轮测试。测试入口加载新版 SQL，每次使用新实例。

```bash
"$ISOLATED_ENTRY" gsql -f /opt/og/openGauss-server/sse_sql/SSE_init.sql
bash "$deliverables/LMD-01/run-module.sh" "$ISOLATED_RUN_DIR/socket" "$ISOLATED_PORT" \
  postgres "$ISOLATED_RUN_DIR/client"
exit
```

正式接入时在目标数据库完成旧版基础初始化后，以 `ON_ERROR_STOP=1` 单独加载 `sse_sql/SSE_v1.sql` 一次。检查指定新版表时运行下列只读检查。旧表使用原有 profile，例如 `fides`。

```bash
gsql -X -h "$socket" -p "$port" -d postgres \
  -v target_schema=public -v target=week_demo -v profile=fides_v1 \
  -f "$deliverables/LMD-01/李孟德-check.sql"
```

模块入口汇总 SQL 退出码及必测 `FAIL` 和 `SKIP` 诊断，通过时以 JSON 输出结论。任务逐项预期、实测结果和日志在交接记录中。[LMD-01 交接](handoff.md) · [LMD-02 真实封装联测](../LMD-02/handoff.md)
