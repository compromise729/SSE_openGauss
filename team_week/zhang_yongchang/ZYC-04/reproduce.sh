#!/usr/bin/env bash
# Run from the outer Linux root shell of an independent, configured checkout.
set -euo pipefail
umask 077
unset PYTHONOPTIMIZE

usage() {
    printf 'Usage %s --repo CHECKOUT [--evidence-dir NEW_DIR]\n' "$0"
}

fail() {
    printf 'ZYC04_REPRODUCE_ERROR=%s\n' "$*" >&2
    exit 2
}

repo=
out=
while (( $# )); do
    case $1 in
        --repo) (( $# >= 2 )) || fail 'missing checkout path'; repo=$2; shift 2 ;;
        --evidence-dir) (( $# >= 2 )) || fail 'missing evidence directory'; out=$2; shift 2 ;;
        --help) usage; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

[[ $EUID == 0 && -z ${ISOLATED_RUN_DIR:-} ]] || fail 'use the outer Linux root shell'
[[ -n $repo && -d $repo ]] || { usage >&2; fail 'use an installed project checkout'; }
repo=$(cd -- "$repo" && pwd -P)
zyc_entry="$repo/team_week/zhang_yongchang/tools/zyc_live_verify_all.sh"
isolation="$repo/team_week/liu_zhuoming/LZM-01/isolated.sh"
yzh_session="$repo/team_week/yan_zihan/YZH-02/yzh02_session.sh"
lmd_entry="$repo/team_week/li_mengde/LMD-03/run.sh"
lzm_tools="$repo/team_week/liu_zhuoming/LZM-04/preflight"
[[ -f $yzh_session ]] || fail 'integrate the YZH-02 tools before running ZYC-04'
for entry in "$zyc_entry" "$isolation" "$lmd_entry" "$lzm_tools/build-current.py" "$lzm_tools/verify.sh"; do
    [[ -r $entry ]] || fail "missing entry: $entry"
done

head=$(cd "$repo" && runuser -u omm -- git rev-parse HEAD) || fail 'omm cannot read the checkout'
if [[ -z $out ]]; then
    out=$(mktemp -d /var/tmp/zyc04-reproduce.XXXXXXXX)
else
    mkdir -m 0700 -- "$out" || fail 'evidence directory must be new'
fi
out=$(cd -- "$out" && pwd -P)
printf 'ZYC04_REPO=%s\nZYC04_EVIDENCE=%s\nZYC04_CHECKOUT=%s\n' "$repo" "$out" "$head" |
    tee "$out/summary.log"

status=0
run() {
    local name=$1 success_line=$2
    shift 2
    local log="$out/$name.log" rc=0
    printf '\n=== %s ===\n' "$name" | tee -a "$out/summary.log"
    "$@" > "$log" 2>&1 || rc=$?
    if (( rc == 0 )) && ! { grep -Fxq -- "$success_line" "$log" &&
                            grep -Fxq 'ISOLATED_SERVER_STOPPED' "$log"; }; then
        printf '%s did not report its required pass and shutdown markers\n' "$name" >&2
        rc=1
    fi
    printf '%s rc=%s log=%s\n' "$name" "$rc" "$log" | tee -a "$out/summary.log"
    if (( rc != 0 )); then
        status=1
    fi
}

run_yzh02() {
    OPENGAUSS_PROJECT_ROOT="$repo" bash "$isolation" init --port 55444 < "$yzh_session"
}

run_lzm04() (
    set +u
    source /home/omm/.sse_opengauss_env || exit
    source /opt/rh/rh-python36/enable || exit
    set -u
    # The verifier sets the build mode. omm must also traverse its parent.
    build=$(mktemp -d /var/tmp/zyc04-lzm04-build.XXXXXXXX) || exit
    printf 'LZM04_BUILD_DIR=%s\n' "$build"
    python3 "$lzm_tools/build-current.py" --repo "$repo" --out "$build" || exit
    bash "$lzm_tools/verify.sh" "$build"
)

run zyc 'ZYC_RESULT=PASS MODULE_CASES=18 BASELINE_LIVE_CASES=18 ZYC_CASES=13' \
    bash "$zyc_entry" --project-root "$repo" --evidence-dir "$out/zyc" --port 55443
run yzh02 'YZH02_RESULT=PASS CASES=18' run_yzh02
run lmd03 'LMD03_RESULT=PASS CASES=11' \
    bash "$lmd_entry" --project-root "$repo" --evidence-dir "$out/lmd03"
run lzm04 'LZM04_CASES=36 RESULT=PASS' run_lzm04

printf 'ZYC04_REPRODUCE_EXIT=%s\n' "$status" | tee -a "$out/summary.log"
exit "$status"
