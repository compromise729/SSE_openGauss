#!/usr/bin/env bash
set -euo pipefail
repo=$OPENGAUSS_PROJECT_ROOT
here="$repo/team_week/li_mengde/LMD-03"
build="$ISOLATED_RUN_DIR/private/lmd03-tools"
export LMD03_BUILD="$build"
mkdir -m 700 "$build"
libs=(-L"$repo/dest/lib" -lpq_ce -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp)
flags=(-std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0 -I"$repo/src/include"
    -I"$repo/src/common/interfaces/libpq" -I"$repo/src/interfaces/libpq")
"$CXX" "${flags[@]}" "$repo/team_week/liu_zhuoming/LZM-03/preflight/initialize-live.cpp" \
    "${libs[@]}" -o "$build/initialize-live"
"$CXX" "${flags[@]}" "$repo/team_week/zhang_yongchang/tools/zyc_decode.cpp" \
    "${libs[@]}" -o "$build/decode"
"$CXX" -std=c++14 -D_GLIBCXX_USE_CXX11_ABI=0 -fPIC -shared \
    -I"$repo/src/include" -I"$repo/src/include/libpq" \
    -I"$repo/sse/opensse-schemes/lib/include" \
    -I"$repo/sse/opensse-schemes/third_party/crypto/src/include" \
    "$repo/team_week/liu_zhuoming/LZM-03/preflight/live-fault-hook.cpp" \
    "$repo/team_week/zhang_yongchang/tools/zyc_trace.cpp" -ldl -o "$build/trace.so"
"$ISOLATED_ENTRY" gsql -c 'fides_setup;' > "$ISOLATED_RUN_DIR/evidence/setup.log" 2>&1
chmod 600 "$ISOLATED_RUN_DIR/client/"*.key
"$ISOLATED_ENTRY" gsql -f "$repo/sse_sql/SSE_v1.sql"
"$build/initialize-live" "host=$ISOLATED_RUN_DIR/socket port=$ISOLATED_PORT dbname=postgres user=omm" \
    "$ISOLATED_RUN_DIR/evidence/lmd03-state"
python3 "$here/cases.py"
