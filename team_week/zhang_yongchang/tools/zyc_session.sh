#!/usr/bin/env bash
# Fed by the outer entry to LZM-01's fresh, private omm session.
set -euo pipefail
repo=$OPENGAUSS_PROJECT_ROOT
tools="$repo/team_week/liu_zhuoming/LZM-03/preflight"
ours="$repo/team_week/zhang_yongchang/tools"
export LIVE_BUILD_DIR="$ISOLATED_RUN_DIR/private/native"
mkdir -m 700 "$LIVE_BUILD_DIR"
ln -s "$repo/dest/bin/gsql" "$LIVE_BUILD_DIR/gsql"
libpq=$(ldd "$repo/dest/bin/gsql" | awk '/^[[:space:]]*libpq(_ce)?[.]so[.]/{print $1}')
[[ $libpq == libpq_ce.so.* || $libpq == libpq.so.* ]]
link=${libpq#lib}
flags=(-std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0 -I"$repo/src/include"
       -I"$repo/src/common/interfaces/libpq" -I"$repo/src/interfaces/libpq")
libs=(-L"$repo/dest/lib" -l"${link%%.*}" -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp)
"${CXX:-g++}" "${flags[@]}" "$tools/initialize-live.cpp" "${libs[@]}" -o "$LIVE_BUILD_DIR/initialize-live"
"${CXX:-g++}" "${flags[@]}" "$ours/zyc_decode.cpp" "${libs[@]}" -o "$LIVE_BUILD_DIR/zyc-decode"
"${CXX:-g++}" -std=c++14 -D_GLIBCXX_USE_CXX11_ABI=0 -pthread -fPIC -shared \
  -I"$repo/src/include" -I"$repo/src/include/libpq" -I"$repo/src/common/interfaces/libpq" \
  -I"$repo/sse/opensse-schemes/lib/include" \
  -I"$repo/sse/opensse-schemes/third_party/crypto/src/include" \
  -I"$BINARYLIBS/dependency/centos7.6_x86_64/openssl/comm/include" \
  "$tools/fault-hook.cpp" "$tools/live-fault-hook.cpp" "$ours/zyc_trace.cpp" -ldl -o "$LIVE_BUILD_DIR/live-fault-hook.so"
isolated.sh gsql -c 'fides_setup;' > "$ISOLATED_RUN_DIR/evidence/setup.log" 2>&1
chmod 600 "$ISOLATED_RUN_DIR/client/"*.key
isolated.sh gsql -f "$repo/sse_sql/SSE_v1.sql"
conn="host=$ISOLATED_RUN_DIR/socket port=$ISOLATED_PORT dbname=postgres user=omm"
"$LIVE_BUILD_DIR/initialize-live" "$conn" "$ISOLATED_RUN_DIR/evidence/v1-state"
python3 "$ours/zyc_cases.py"
isolated.sh stop
