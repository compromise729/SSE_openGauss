#!/usr/bin/env bash
set -euo pipefail
umask 077
[[ $# == 1 && $EUID == 0 ]] || { echo 'Usage (outer root): bash run-live.sh --native | LIVE_BUILD_DIR' >&2; exit 2; }
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "$here/../../../.." && pwd -P)
build=$1
if [[ $build != --native ]]; then build=$(cd -- "$build" && pwd -P); fi
out=$(mktemp -d /var/tmp/state-live-evidence.XXXXXXXX)
echo "LIVE_EVIDENCE_DIR=$out"
find "$repo/dest/bin" "$repo/dest/lib" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/installation.before.sha256"
find "$repo/sse_storage" "$repo/sse_server_storage" -type f -print0 | sort -z | xargs -0 -r sha256sum > "$out/original-state.before.private"
export OPENGAUSS_PROJECT_ROOT="$repo"
rc=0
unshare -m --propagation private bash -euc '
    if [[ $1 != --native ]]; then
        mount --make-rprivate /
        mount --bind "$1/gaussdb" "$2/dest/bin/gaussdb"
    fi
    {
        printf "export LIVE_BUILD_DIR=%q\n" "$1"
        cat <<"SESSION"
set -euo pipefail
if [[ $LIVE_BUILD_DIR == --native ]]; then
    repo=$OPENGAUSS_PROJECT_ROOT
    tools=$repo/team_week/liu_zhuoming/LZM-03/preflight
    export LIVE_BUILD_DIR="$ISOLATED_RUN_DIR/private/native"
    mkdir -m 700 "$LIVE_BUILD_DIR"
    ln -s "$repo/dest/bin/gsql" "$LIVE_BUILD_DIR/gsql"
    libpq=$(ldd "$repo/dest/bin/gsql" | awk '"'"'/^[[:space:]]*libpq(_ce)?[.]so[.]/{print $1}'"'"')
    [[ $libpq == libpq_ce.so.* || $libpq == libpq.so.* ]]
    link=${libpq#lib}
    "${CXX:-g++}" -std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0 -I"$repo/src/include" \
        -I"$repo/src/common/interfaces/libpq" -I"$repo/src/interfaces/libpq" \
        "$tools/initialize-live.cpp" -L"$repo/dest/lib" -l"${link%%.*}" \
        -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp -o "$LIVE_BUILD_DIR/initialize-live"
    "${CXX:-g++}" -std=c++14 -D_GLIBCXX_USE_CXX11_ABI=0 -pthread -fPIC -shared \
        -I"$repo/src/include" -I"$repo/src/include/libpq" \
        -I"$repo/sse/opensse-schemes/lib/include" \
        -I"$repo/sse/opensse-schemes/third_party/crypto/src/include" \
        -I"$BINARYLIBS/dependency/centos7.6_x86_64/openssl/comm/include" \
        "$tools/fault-hook.cpp" "$tools/live-fault-hook.cpp" -ldl -o "$LIVE_BUILD_DIR/live-fault-hook.so"
fi
isolated.sh gsql -c "fides_setup;" > "$ISOLATED_RUN_DIR/evidence/setup.log" 2>&1
chmod 600 "$ISOLATED_RUN_DIR/client/"*.key
isolated.sh gsql -f "$OPENGAUSS_PROJECT_ROOT/sse_sql/SSE_v1.sql"
conn="host=$ISOLATED_RUN_DIR/socket port=$ISOLATED_PORT dbname=postgres user=omm"
"$LIVE_BUILD_DIR/initialize-live" "$conn" "$ISOLATED_RUN_DIR/evidence/v1-state"
python3 "$OPENGAUSS_PROJECT_ROOT/team_week/liu_zhuoming/LZM-03/preflight/live-cases.py"
isolated.sh stop
SESSION
    } | bash "$2/team_week/liu_zhuoming/LZM-01/isolated.sh" init
' _ "$build" "$repo" > "$out/live.log" 2>&1 || rc=$?
find "$repo/dest/bin" "$repo/dest/lib" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/installation.after.sha256"
find "$repo/sse_storage" "$repo/sse_server_storage" -type f -print0 | sort -z | xargs -0 -r sha256sum > "$out/original-state.after.private"
cmp "$out/installation.before.sha256" "$out/installation.after.sha256"
cmp "$out/original-state.before.private" "$out/original-state.after.private"
printf '%s\n' 'PASS installed programs/libraries unchanged' 'PASS original client/server state bytes unchanged' > "$out/environment.log"
cat "$out/environment.log"
grep -E '^(RUN_DIR=|PASS LIVE|FAIL LIVE|LIVE_RESULT=|ISOLATED_SERVER_STOPPED)' "$out/live.log" || true
printf 'LIVE_EXIT=%s\n' "$rc"
if (( rc != 0 )); then tail -n 25 "$out/live.log"; fi
exit "$rc"
