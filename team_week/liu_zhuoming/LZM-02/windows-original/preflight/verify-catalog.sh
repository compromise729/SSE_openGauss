#!/usr/bin/env bash
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "${OPENGAUSS_PROJECT_ROOT:-$here/../../../..}" && pwd -P)
[[ $EUID == 0 ]] || { echo 'Run in the outer root shell.' >&2; exit 2; }
cd "$repo"
for required in "$repo/src/bin/psql/gsql" "$repo/src/common/interfaces/libpq/libpq_ce.so.5"; do
    if [[ ! -r $required ]]; then
        echo "FAIL CATALOG prerequisites: missing rebuilt artifact $required" >&2
        exit 1
    fi
done
[[ -x $repo/src/bin/psql/gsql ]] || { echo 'FAIL CATALOG prerequisites: gsql is not executable' >&2; exit 1; }
out=$(mktemp -d /tmp/lzm02-catalog.XXXXXXXX)
echo "EVIDENCE_DIR=$out"
chmod 0711 "$out"
export OPENGAUSS_PROJECT_ROOT="$repo"
bash "$here/build-checker.sh" "$out/check-state"
src="$repo/src/common/interfaces/libpq"
"${CXX:-g++}" -std=c++11 -pthread -Wall -Wextra -Werror -I"$repo/src/include" \
  "$here/catalog-fixture.cpp" "$src/row_state_v1.cpp" "$src/row_state_catalog.cpp" \
  -L"$repo/dest/lib" -Wl,-rpath,"$repo/dest/lib" -lpq_ce -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp -o "$out/catalog-fixture"
chmod 0755 "$out/"{check-state,catalog-fixture}
# The v1 fixture is created inside a new LZM-01 run, separate from its legacy client.
{
  printf 'test_tools=%q\n' "$out"
  printf 'source_repo=%q\n' "$repo"
  cat <<'SESSION'
state="$ISOLATED_RUN_DIR/evidence/v1-state"
mkdir -m 0700 "$state"
conn="host=$ISOLATED_RUN_DIR/socket port=$ISOLATED_PORT dbname=postgres user=omm"
SSE_V1_STATE_DIR="$state" "$test_tools/catalog-fixture" "$conn"
snapshot() {
    (cd "$state"; find . -maxdepth 1 -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum)
}
snapshot > "$ISOLATED_RUN_DIR/evidence/v1-check.before"
args=(--state-dir "$state" --conninfo "$conn" --table week_demo)
"$test_tools/check-state" "${args[@]}" --keyword apple
rc=0
"$test_tools/check-state" "${args[@]}" --keyword unknown || rc=$?
[[ $rc == 1 ]]
rc=0
"$test_tools/check-state" "${args[@]}" >/dev/null || rc=$?
[[ $rc == 2 ]]
echo 'PASS CATALOG-02: CLI reports exit 0/1/2'
snapshot > "$ISOLATED_RUN_DIR/evidence/v1-check.after"
cmp "$ISOLATED_RUN_DIR/evidence/v1-check.before" "$ISOLATED_RUN_DIR/evidence/v1-check.after"
echo 'PASS CATALOG-05: CLI leaves all v1 state digests unchanged'
isolated.sh gsql -c "UPDATE public.sse_v1_catalog SET protocol_version=2 WHERE logical_table='week_demo';" >/dev/null
rc=0
"$test_tools/check-state" "${args[@]}" --keyword apple > "$ISOLATED_RUN_DIR/evidence/v1-version.log" || rc=$?
[[ $rc == 3 ]]
grep -q '"status":"state_invalid"' "$ISOLATED_RUN_DIR/evidence/v1-version.log"
isolated.sh gsql -c "UPDATE public.sse_v1_catalog SET protocol_version=1 WHERE logical_table='week_demo';" >/dev/null
echo 'PASS CATALOG-06: CLI rejects mismatched server table version'

for relation in week_demo_data week_demo_index; do
    isolated.sh gsql -c "ALTER TABLE public.$relation DROP CONSTRAINT ${relation}_pkey;" >/dev/null
    rc=0
    "$test_tools/check-state" "${args[@]}" --keyword apple > "$ISOLATED_RUN_DIR/evidence/$relation-primary-key.log" || rc=$?
    [[ $rc == 3 ]]
    grep -q '"status":"state_invalid"' "$ISOLATED_RUN_DIR/evidence/$relation-primary-key.log"
    key=rid
    if [[ $relation == week_demo_index ]]; then key=ut; fi
    isolated.sh gsql -c "ALTER TABLE public.$relation ADD PRIMARY KEY ($key);" >/dev/null
done
echo 'PASS CATALOG-07: missing rid or ut primary key is rejected'

rc=0
SSE_V1_STATE_DIR="$state" LD_LIBRARY_PATH="$source_repo/src/common/interfaces/libpq:$LD_LIBRARY_PATH" \
  "$source_repo/src/bin/psql/gsql" -X -h "$ISOLATED_RUN_DIR/socket" -p "$ISOLATED_PORT" -d postgres -U omm \
  -c 'SELECT 1;' > "$ISOLATED_RUN_DIR/evidence/v1-startup.log" 2>&1 || rc=$?
[[ $rc != 0 ]]
grep -q 'Fides unsupported: v1 request routing requires the ZYC integration' "$ISOLATED_RUN_DIR/evidence/v1-startup.log"
echo 'PASS CATALOG-03: rebuilt gsql validates fixture before routing gate'
printf 'interrupted fixture\n' > "$state/operation.v1"
rc=0
SSE_V1_STATE_DIR="$state" LD_LIBRARY_PATH="$source_repo/src/common/interfaces/libpq:$LD_LIBRARY_PATH" \
  "$source_repo/src/bin/psql/gsql" -X -h "$ISOLATED_RUN_DIR/socket" -p "$ISOLATED_PORT" -d postgres -U omm \
  -c 'SELECT 1;' > "$ISOLATED_RUN_DIR/evidence/v1-interrupted.log" 2>&1 || rc=$?
[[ $rc != 0 ]]
grep -q 'Fides incomplete_operation' "$ISOLATED_RUN_DIR/evidence/v1-interrupted.log"
echo 'PASS CATALOG-04: rebuilt gsql refuses unfinished marker before any user SQL'
rc=0
"$test_tools/check-state" "${args[@]}" --keyword apple || rc=$?
[[ $rc == 3 ]]
isolated.sh stop
SESSION
} | bash "$here/../../LZM-01/isolated.sh" init > "$out/live.log" 2>&1 || { tail -n 28 "$out/live.log"; exit 1; }
grep -E '^(RUN_DIR=|PASS CATALOG|ISOLATED_SERVER_STOPPED$|\{"status":)' "$out/live.log"
