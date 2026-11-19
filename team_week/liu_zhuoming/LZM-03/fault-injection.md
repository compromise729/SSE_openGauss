# LZM-03｜实际故障注入与复验

原始注入记录对应 `0671a79`。复验当前版本时使用 [原生复跑步骤](env.md)，当前版本在同一数据库事务内回滚索引取出和重插，故障仍保留客户端标记。[契约修复与原生复验](契约修复与原生复验.md)

## 一、已经执行的真实故障

以下均通过真实 gsql 请求入口执行，结果见 [live-cases.json](result/live-cases.json) 和 [live/](result/live/)。

| 用例 | 注入位置 | 已观察到的结果 |
| --- | --- | --- |
| LIVE-06/07 | 对 operation.v1 的 renameat 返回 EIO，分别执行 INSERT、SELECT。 | 客户端非零；未推进计数、未发业务请求；状态和数据库摘要一致。 |
| LIVE-08 | 在同一实例的状态副本中截断计数文件。 | state_invalid，无业务发送，文件未补建。 |
| LIVE-09～11 | 分别移走 AEAD 行密钥、位置密钥和 key_id，已有数据库数据不动。 | key_invalid，未生成新钥，计数和数据库未变。 |
| LIVE-12/13 | 实际 Fides search_request 生成结果后，清空 token 或 derivation_key。 | protocol_invalid，无搜索取出；标记保留，新进程 incomplete_operation。 |
| LIVE-15 | 数据库 BEFORE INSERT 触发器让真实索引重插失败。 | db_failed，无业务回查/展示；保留标记；apple 的两个索引已取出，数据行 4、剩余索引 2；新进程阻断。 |

完整一键复现：按 [env.md](env.md) 运行 verify-all.sh。18 个模块夹具同时保留，模块故障方式和结果在 result/cases/、module-results.json。

## 二、手动新建一个可丢弃的联调实例

需要观察某个故障时，可使用已构建产物。先在外层 Linux root 终端执行：

~~~bash
repo=/opt/og/lzm03-pr11-review.Gtp3l3v2/source
OPENGAUSS_PROJECT_ROOT="$repo" bash "$repo/team_week/liu_zhuoming/LZM-01/isolated.sh" init
~~~

该命令进入新个人实例的 omm 会话。从上一次原生复验日志中取实际 `RUN_DIR`，复用同一构建的初始化和故障注入工具。

在出现的 omm 会话中执行：

~~~bash
id -un
read -r -p '填入上一次原生复验的 RUN_DIR：' verified_run
build="$verified_run/private/native"
test -x "$build/initialize-live"
isolated.sh gsql -c 'fides_setup;'
chmod 600 "$ISOLATED_RUN_DIR/client/"*.key
isolated.sh gsql -f "$OPENGAUSS_PROJECT_ROOT/sse_sql/SSE_v1.sql"
conn="host=$ISOLATED_RUN_DIR/socket port=$ISOLATED_PORT dbname=postgres user=omm"
state="$ISOLATED_RUN_DIR/evidence/v1-state"
"$build/initialize-live" "$conn" "$state"
connection=(-X -h "$ISOLATED_RUN_DIR/socket" -p "$ISOLATED_PORT" -d postgres -U omm -v ON_ERROR_STOP=1)
vsql() {
  SSE_V1_STATE_DIR="$state" STATE_TRACE=1 LD_PRELOAD="$build/live-fault-hook.so"     "$build/gsql" "${connection[@]}" "$@"
}
vsql -c "CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text) WITH (sse_storage='ROW_AEAD_V1');"
vsql -c "INSERT INTO week_demo VALUES ('1','apple','red apple'),('2','apple','green apple');"
vsql -c "SELECT * FROM week_demo WHERE keyword='apple';"
~~~

`id -un` 应输出 omm。`state` 是本次新版密钥、计数和映射目录，首次初始化要求此前不存在；`connection` 保存本次 Unix socket 的连接参数；`vsql` 将这些参数及新版状态目录传给临时 gsql。最后一条应返回编号 1、2 各一次。

## 三、手动注入标记写失败

仍在上述 omm 会话执行：

~~~bash
sha256sum "$state/fides_client.mat" "$state/sse_table.mat" > "$ISOLATED_RUN_DIR/evidence/marker.before"
isolated.sh gsql -c 'SELECT count(*) FROM public.week_demo_index;' > "$ISOLATED_RUN_DIR/evidence/index.before"
rc=0
SSE_V1_STATE_DIR="$state" STATE_TRACE=1 STATE_FAULT=marker-rename LD_PRELOAD="$build/live-fault-hook.so"   "$build/gsql" "${connection[@]}" -c "SELECT * FROM week_demo WHERE keyword='apple';"   > "$ISOLATED_RUN_DIR/evidence/marker.stdout" 2> "$ISOLATED_RUN_DIR/evidence/marker.stderr" || rc=$?
printf '客户端退出码=%s
' "$rc"
cat "$ISOLATED_RUN_DIR/evidence/marker.stderr"
sha256sum "$state/fides_client.mat" "$state/sse_table.mat" > "$ISOLATED_RUN_DIR/evidence/marker.after"
cmp "$ISOLATED_RUN_DIR/evidence/marker.before" "$ISOLATED_RUN_DIR/evidence/marker.after"
isolated.sh gsql -c 'SELECT count(*) FROM public.week_demo_index;' > "$ISOLATED_RUN_DIR/evidence/index.after"
cmp "$ISOLATED_RUN_DIR/evidence/index.before" "$ISOLATED_RUN_DIR/evidence/index.after"
~~~

预期非零退出、state_invalid 和 operation.v1 保存错误；没有 begin_durable/counter_advanced/search_take_sent；两次 cmp 均返回 0。这证明错误发生在实际 Fides 计数推进和索引取出之前。把 -c 中 SQL 换为 INSERT 可复验插入分支。

## 四、手动注入异常空令牌

在健康基线上复制一份状态，用于同一实例的故障观察。该故障在业务发送前停止，不改变数据库：

~~~bash
healthy_state=$state
fault_state="$ISOLATED_RUN_DIR/evidence/empty-token-state"
cp -a -- "$healthy_state" "$fault_state"
rc=0
SSE_V1_STATE_DIR="$fault_state" STATE_TRACE=1 STATE_FAULT=empty-token LD_PRELOAD="$build/live-fault-hook.so"   "$build/gsql" "${connection[@]}" -c "SELECT * FROM week_demo WHERE keyword='apple';"   > "$ISOLATED_RUN_DIR/evidence/token.stdout" 2> "$ISOLATED_RUN_DIR/evidence/token.stderr" || rc=$?
printf '异常令牌退出码=%s
' "$rc"
cat "$ISOLATED_RUN_DIR/evidence/token.stderr"
test -f "$fault_state/operation.v1"
rc=0
SSE_V1_STATE_DIR="$fault_state" "$build/gsql" "${connection[@]}"   -c "SELECT * FROM week_demo WHERE keyword='apple';" || rc=$?
printf '重开退出码=%s
' "$rc"
~~~

预期第一次 protocol_invalid，第二次 incomplete_operation，均非零且不展示业务行。将 STATE_FAULT 改为 empty-derivation 并使用新的副本目录，可复验另一分支。健康 unknown 不启用故障，应正常返回零行；坏计数则应报 state_invalid，三者分开处理。

## 五、手动注入已有数据缺钥

对上述健康原状态的另一个副本执行，保留被移走文件：

~~~bash
missing_state="$ISOLATED_RUN_DIR/evidence/missing-key-state"
cp -a -- "$healthy_state" "$missing_state"
mv -- "$missing_state/row_aead_v1.key" "$missing_state/row_aead_v1.key.held"
rc=0
SSE_V1_STATE_DIR="$missing_state" "$build/gsql" "${connection[@]}"   -c "SELECT * FROM week_demo WHERE keyword='apple';" || rc=$?
printf '缺钥退出码=%s
' "$rc"
test ! -e "$missing_state/row_aead_v1.key"
~~~

预期 key_invalid、非零退出，密钥没有被补建。副本用于发送前拒绝验证，不用于在同一数据库上并发继续业务。

## 六、停机与保留现场

~~~bash
isolated.sh stop
exit
~~~

保留数据库、两侧状态、副本及日志。一键验证将真实重插错误放在最后，当前版本会回滚数据库事务并保留 `operation.v1`。复验需要重新初始化时，使用新的可丢弃测试实例。
