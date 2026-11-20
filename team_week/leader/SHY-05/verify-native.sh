#!/usr/bin/env bash
# Run with the freshly installed candidate, from the outer Linux root shell.
set -euo pipefail
umask 077

if [[ $EUID != 0 || $# != 2 || -n ${ISOLATED_RUN_DIR:-} ]]; then
    printf 'Usage (outer Linux root) bash %s CHECKOUT NEW_EVIDENCE_DIR\n' "$0" >&2
    exit 2
fi
repo=$(cd -- "$1" && pwd -P)
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
candidate=5c484c593d3520fa62046871371446d5cd5b7c70
head=$(cd "$repo" && runuser -u omm -- git rev-parse HEAD)
if [[ $head != "$candidate" ]]; then
    printf 'Expected candidate %s, actual %s\n' "$candidate" "$head" >&2
    exit 2
fi
probe="$here/native-fixture.py"
runuser -u omm -- test -r "$probe"
mkdir -m 0700 -- "$2"
out=$(cd -- "$2" && pwd -P)
printf '%s\n' "$head" > "$out/source-commit.txt"
printf 'SHY05_NATIVE_EVIDENCE=%s\n' "$out"
entry="$repo/team_week/liu_zhuoming/LZM-01/isolated.sh"

run_probe() {
    local mode=$1 port=$2 rc=0 run
    local log="$out/$mode.log"
    {
        printf 'export SHY05_PROBE=%q SHY05_MODE=%q\n' "$probe" "$mode"
        cat <<'SESSION'
set -euo pipefail
unset PYTHONOPTIMIZE
repo=$OPENGAUSS_PROJECT_ROOT
if [[ $SHY05_MODE == row-v1 ]]; then
    isolated.sh gsql -c 'fides_setup;' > "$ISOLATED_RUN_DIR/evidence/setup-v1.log" 2>&1
    chmod 600 "$ISOLATED_RUN_DIR/client/"*.key
    isolated.sh gsql -f "$repo/sse_sql/SSE_v1.sql" > "$ISOLATED_RUN_DIR/evidence/schema-v1.log" 2>&1
    initializer="$ISOLATED_RUN_DIR/private/initialize-live"
    "$CXX" -std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0 -I"$repo/src/include" \
      -I"$repo/src/common/interfaces/libpq" -I"$repo/src/interfaces/libpq" \
      "$repo/team_week/liu_zhuoming/LZM-03/preflight/initialize-live.cpp" \
      -L"$repo/dest/lib" -lpq_ce -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp -o "$initializer"
    "$initializer" "host=$ISOLATED_RUN_DIR/socket port=$ISOLATED_PORT dbname=postgres user=omm" \
      "$ISOLATED_RUN_DIR/evidence/shy05-state"
else
    isolated.sh gsql -f "$repo/sse_sql/SSE_init.sql" > "$ISOLATED_RUN_DIR/evidence/schema-legacy.log" 2>&1
    isolated.sh gsql -f "$repo/team_week/leader/SHY-01/sql/01-write.sql" > "$ISOLATED_RUN_DIR/evidence/fixture-legacy.log" 2>&1
fi
python3 "$SHY05_PROBE" "$SHY05_MODE"
isolated.sh stop
SESSION
    } | OPENGAUSS_PROJECT_ROOT="$repo" bash "$entry" init --port "$port" > "$log" 2>&1 || rc=$?
    if (( rc != 0 )); then
        tail -n 24 "$log" >&2
        return "$rc"
    fi
    run=$(sed -n 's/^RUN_DIR=//p' "$log" | sed -n '1p')
    [[ -n $run && ! -e $run/db/instance/postmaster.pid ]] || return 1
    grep -Fx "SHY05_NATIVE_$mode=PASS" "$log" || return 1
    grep -Fx 'ISOLATED_SERVER_STOPPED' "$log" || return 1
    cp -- "$run/evidence/shy05-$mode.json" "$out/$mode.json"
}

rc=0
run_probe row-v1 55461 || rc=1
run_probe legacy 55462 || rc=1
printf 'SHY05_NATIVE_EXIT=%s\n' "$rc"
exit "$rc"
