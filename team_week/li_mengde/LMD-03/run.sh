#!/usr/bin/env bash
# Run as outer Linux root with an installed SHY-04 checkout.
set -Eeuo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$here/../../..
port=55472
out=
while (( $# )); do
    case $1 in
        --project-root) repo=$2; shift 2 ;;
        --evidence-dir) out=$2; shift 2 ;;
        --port) port=$2; shift 2 ;;
        *) echo 'Usage: bash run.sh --project-root PATH [--evidence-dir NEW_DIR] [--port PORT]' >&2; exit 2 ;;
    esac
done
[[ $EUID == 0 && -z ${ISOLATED_RUN_DIR:-} ]] || { echo 'Use outer Linux root.' >&2; exit 2; }
[[ $port =~ ^[0-9]{4,5}$ ]] && (( 10#$port > 1023 && 10#$port < 65535 )) ||
    { echo 'Port must allow two consecutive non-privileged ports.' >&2; exit 2; }
port=$((10#$port))
repo=$(cd -- "$repo" && pwd -P)
[[ -x "$repo/dest/bin/gsql" && -x "$repo/dest/bin/gaussdb" ]]
suite="$repo/team_week/li_mengde/LMD-03"
baseline=5c8b2b03b482e203ac65a6f989a1b669126f263c
head=$(
    cd "$repo"
    runuser -u omm -- git rev-parse HEAD
)
if ! (
    cd "$repo"
    runuser -u omm -- git merge-base --is-ancestor "$baseline" "$head" &&
    runuser -u omm -- git diff --quiet "$baseline" "$head" -- src sse sse_sql contrib \
        team_week/yan_zihan/YZH-01/include team_week/yan_zihan/YZH-01/src &&
    runuser -u omm -- git diff --quiet HEAD -- sse_sql sse src/bin/psql/common.cpp src/bin/psql/startup.cpp \
        src/common/interfaces/libpq src/gausskernel/process/tcop src/include/libpq \
        team_week/yan_zihan/YZH-01/include team_week/yan_zihan/YZH-01/src
); then
    echo 'LMD-03 requires the fixed SHY-04 runtime source.' >&2
    exit 2
fi
if [[ -z $out ]]; then
    out=$(mktemp -d /var/tmp/lmd03-evidence.XXXXXXXX)
else
    mkdir -m 700 -- "$out"
fi
out=$(cd -- "$out" && pwd -P)
echo "LMD03_EVIDENCE_DIR=$out"
trap 'status=$?; printf "{\"task\":\"LMD-03\",\"exit_code\":%s}\n" "$status" > "$out/runner.json"' EXIT
export OPENGAUSS_PROJECT_ROOT="$repo"
if ! command -v python3 >/dev/null; then
    set +u
    source /opt/rh/rh-python36/enable
    set -u
fi
export PYTHONIOENCODING=utf-8
{
    cd "$repo"
    printf 'CHECKOUT_HEAD=%s\nSOURCE_BASELINE=%s\n' "$head" "$baseline"
    sha256sum dest/bin/gaussdb dest/bin/gsql dest/lib/libpq_ce.so.5.5 sse_sql/SSE_init.sql sse_sql/SSE_v1.sql
    sha256sum "$suite"/{run.sh,session.sh,cases.py,db-boundaries.sql}
} > "$out/fingerprint.txt"
entry="$repo/team_week/liu_zhuoming/LZM-01/isolated.sh"
{
    printf '%s\n' '"$ISOLATED_ENTRY" gsql -f "$OPENGAUSS_PROJECT_ROOT/sse_sql/SSE_init.sql" > "$ISOLATED_RUN_DIR/client/bootstrap-sse.log" 2>&1'
    printf 'bash %q "$ISOLATED_RUN_DIR/socket" "$ISOLATED_PORT" postgres "$ISOLATED_RUN_DIR/client"\n' \
        "$repo/team_week/li_mengde/LMD-01/run-module.sh"
} |
    bash "$entry" init --port "$port" > "$out/module.log" 2>&1
grep -F '{"task":"LMD-01","phase":"complete","exit_code":0,"passed":true}' "$out/module.log"
grep -Fx 'ISOLATED_SERVER_STOPPED' "$out/module.log"
{
    printf 'export LMD03_SOURCE_BASELINE=%q LMD03_CHECKOUT_HEAD=%q\n' "$baseline" "$head"
    cat "$suite/session.sh"
} |
    bash "$entry" init --port "$((port + 1))" > "$out/live.log" 2>&1
run=$(sed -n 's/^RUN_DIR=//p' "$out/live.log" | head -1)
[[ -n $run && ! -e "$run/db/instance/postmaster.pid" ]]
cp -R -- "$run/evidence/lmd03" "$out/cases"
grep -Fx 'ISOLATED_SERVER_STOPPED' "$out/live.log"
python3 - "$out" "$head" "$baseline" <<'PY'
import json
import sys
from pathlib import Path
root = Path(sys.argv[1])
result = json.loads((root / "cases/results.json").read_text())
cases = result["cases"]
expected = ["DB-{:02d}".format(number) for number in range(1, 12)]
if (result["result"] != "PASS" or [case["case"][:5] for case in cases] != expected
        or any(case["result"] != "PASS" for case in cases)
        or result["checkout_head"] != sys.argv[2] or result["source_commit"] != sys.argv[3]):
    raise SystemExit("LMD-03 required case matrix or version is incomplete")
print("LMD03_RESULT=PASS CASES={}".format(len(cases)))
PY
