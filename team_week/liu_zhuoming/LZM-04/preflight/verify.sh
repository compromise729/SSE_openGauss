#!/usr/bin/env bash
set -euo pipefail
umask 077
[[ $EUID == 0 && $# == 1 ]] || { echo 'Usage: bash verify.sh BUILD_DIR'; exit 2; }
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "$here/../../../.." && pwd -P)
build=$(cd -- "$1" && pwd -P)
python3 "$here/build-current.py" --repo "$repo" --out "$build" --verify-inputs
out=$(mktemp -d /var/tmp/lzm04-evidence.XXXXXXXX)
echo "EVIDENCE_DIR=$out"
cp -- "$build/build-input.json" "$out/build-input.json"
# Exercise supported relocation and invocation outside the checkout on every run.
chmod 0711 "$out"
mkdir -m 0755 "$out/isolated-tools"
install -m 0755 "$repo/team_week/liu_zhuoming/LZM-01/isolated.sh" "$out/isolated-tools/isolated.sh"
cp -a "$repo/team_week/liu_zhuoming/LZM-01/preflight" "$out/isolated-tools/preflight"
echo "ISOLATION_ENTRY=$out/isolated-tools/isolated.sh"
find "$repo/dest/bin" "$repo/dest/lib" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/install.before"
find "$repo/sse_storage" "$repo/sse_server_storage" -type f -print0 | sort -z | xargs -0 -r sha256sum > "$out/state.before.private"
find "$repo/sse_storage" "$repo/sse_server_storage" -printf '%p %y %m %U:%G\n' | sort > "$out/modes.before.private"
flags=(-std=c++14 -D_GLIBCXX_USE_CXX11_ABI=0 -pthread -I"$repo/src/include" -I"$repo/src/include/libpq"
 -I"$repo/sse/opensse-schemes/lib/include" -I"$repo/sse/opensse-schemes/third_party/crypto/src/include"
 -I"$BINARYLIBS/dependency/centos7.6_x86_64/openssl/comm/include")
"${CXX:-g++}" "${flags[@]}" -fPIC -shared "$here/faults.cpp" -ldl -o "$build/faults04.so"
"${CXX:-g++}" "${flags[@]}" "$here/legacy-control.cpp" -L"$build" -Wl,-rpath,"$build" -lrow_request_verify \
 -L"$repo/dest/lib" -Wl,-rpath,"$repo/dest/lib" -lpq_ce -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp -o "$build/legacy-control"
chmod 0755 "$build/faults04.so" "$build/legacy-control"
rc=0
unshare -m --propagation private bash -euc '
 mount --make-rprivate /
 mount --bind "$1/gaussdb" "$2/dest/bin/gaussdb"
 {
   printf "export LZM04_BUILD=%q\n" "$1"
   cat <<"SESSION"
isolated.sh gsql -c "fides_setup;" >/dev/null
chmod 600 "$ISOLATED_RUN_DIR/client/"*.key
python3 "$OPENGAUSS_PROJECT_ROOT/team_week/liu_zhuoming/LZM-04/preflight/cases.py"
isolated.sh stop
SESSION
 } | (cd /var/tmp; OPENGAUSS_PROJECT_ROOT="$2" bash "$3/isolated.sh" init)
' _ "$build" "$repo" "$out/isolated-tools" > "$out/live.log" 2>&1 || rc=$?
find "$repo/dest/bin" "$repo/dest/lib" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/install.after"
find "$repo/sse_storage" "$repo/sse_server_storage" -type f -print0 | sort -z | xargs -0 -r sha256sum > "$out/state.after.private"
find "$repo/sse_storage" "$repo/sse_server_storage" -printf '%p %y %m %U:%G\n' | sort > "$out/modes.after.private"
cmp "$out/install.before" "$out/install.after"
cmp "$out/state.before.private" "$out/state.after.private"
cmp "$out/modes.before.private" "$out/modes.after.private"
printf '%s\n' 'PASS installed binaries and libraries unchanged' 'PASS original client/server state bytes unchanged' 'PASS original client/server file inventory, permissions and owners unchanged' > "$out/environment.log"
cat "$out/environment.log"
grep -E '^(RUN_DIR=|PASS 04|LZM04_CASES|ISOLATED_SERVER_STOPPED)' "$out/live.log" || true
if (( rc == 0 )) && { ! grep -Fxq 'LZM04_CASES=36 RESULT=PASS' "$out/live.log" ||
                       ! grep -Fxq 'ISOLATED_SERVER_STOPPED' "$out/live.log"; }; then
    echo 'LZM-04 evidence is incomplete' >&2
    rc=1
fi
if (( rc != 0 )); then tail -n 25 "$out/live.log"; fi
echo "VERIFY_EXIT=$rc"
exit "$rc"
