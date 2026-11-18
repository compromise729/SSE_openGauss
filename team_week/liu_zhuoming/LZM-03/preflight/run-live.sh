#!/usr/bin/env bash
set -euo pipefail
umask 077
[[ $# == 1 && $EUID == 0 ]] || { echo 'Usage (outer root): bash run-live.sh LIVE_BUILD_DIR' >&2; exit 2; }
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "$here/../../../.." && pwd -P)
build=$(cd -- "$1" && pwd -P)
out=$(mktemp -d /var/tmp/state-live-evidence.XXXXXXXX)
echo "LIVE_EVIDENCE_DIR=$out"
find "$repo/dest/bin" "$repo/dest/lib" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/installation.before.sha256"
find "$repo/sse_storage" "$repo/sse_server_storage" -type f -print0 | sort -z | xargs -0 -r sha256sum > "$out/original-state.before.private"
export LIVE_BUILD_DIR="$build"
rc=0
# The new backend is mounted only in a child mount namespace. The on-disk
# installed program and every installed library retain their original bytes.
unshare -m --propagation private bash -euc '
    mount --make-rprivate /
    mount --bind "$1/gaussdb" "$2/dest/bin/gaussdb"
    {
        printf "export LIVE_BUILD_DIR=%q\n" "$1"
        cat <<"SESSION"
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
