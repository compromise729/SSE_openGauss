#!/usr/bin/env bash
# Outer Linux root entry. Only public synthetic-fixture evidence is exported.
set -Eeuo pipefail
umask 077
unset PYTHONOPTIMIZE
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=${OPENGAUSS_PROJECT_ROOT:-$here/../../..}
out=
port=55443
fail() { printf 'ZYC_RESULT=FAIL %s\n' "$*" >&2; exit 1; }
while (( $# )); do
  case $1 in
    --project-root) (( $# >= 2 )) || fail 'missing project root'; repo=$2; shift 2 ;;
    --evidence-dir) (( $# >= 2 )) || fail 'missing evidence directory'; out=$2; shift 2 ;;
    --port) (( $# >= 2 )) || fail 'missing port'; port=$2; shift 2 ;;
    --help) echo 'Usage (outer root): bash zyc_live_verify_all.sh [--project-root PATH] [--evidence-dir NEW_DIR] [--port PORT]'; exit 0 ;;
    *) fail "unknown argument: $1" ;;
  esac
done
[[ $EUID == 0 && -z ${ISOLATED_RUN_DIR:-} ]] || fail 'run once from the outer Linux root terminal'
[[ $port =~ ^[0-9]+$ && ${#port} -le 5 ]] || fail 'invalid port'
(( 10#$port >= 1024 && 10#$port <= 65535 )) || fail 'invalid port'
repo=$(cd -- "$repo" && pwd -P) || fail 'project root does not exist'
# The released LZM loader parses ldd columns and cannot represent whitespace
# in its selected install path. A private bind alias keeps the checkout bytes
# intact and lets both released validation and this entry use the same install.
if [[ $repo == *[[:space:]]* ]]; then
  exec unshare -m --propagation private bash -euc '
    mount --make-rprivate /
    alias=$(mktemp -d /var/tmp/zyc-checkout.XXXXXXXX)
    cleanup() { umount "$alias"; rmdir "$alias"; }
    trap cleanup EXIT
    mount --bind "$1" "$alias"
    export ZYC_REQUESTED_PROJECT_ROOT="$1"
    args=(--project-root "$alias" --port "$2")
    [[ -z $3 ]] || args+=(--evidence-dir "$3")
    bash "$alias/team_week/zhang_yongchang/tools/zyc_live_verify_all.sh" "${args[@]}"
  ' _ "$repo" "$port" "$out"
fi
here="$repo/team_week/zhang_yongchang/tools"
for f in dest/bin/gsql dest/bin/gaussdb dest/lib/libpq_ce.so.5.5 \
  sse_sql/SSE_v1.sql team_week/liu_zhuoming/LZM-03/preflight/initialize-live.cpp \
  team_week/liu_zhuoming/LZM-03/preflight/verify-all.sh \
  team_week/liu_zhuoming/LZM-01/isolated.sh \
  team_week/zhang_yongchang/tools/zyc_session.sh; do
  [[ -f $repo/$f ]] || fail "missing required build/tool: $repo/$f"
done
if ! command -v python3 >/dev/null; then
  [[ -r /opt/rh/rh-python36/enable ]] || fail 'Python >=3.6 required'
  set +u; source /opt/rh/rh-python36/enable; set -u
fi
python3 -c 'import sys; assert sys.version_info >= (3,6)'
export PYTHONIOENCODING=utf-8
if [[ -z ${CXX:-} ]]; then
  CXX=$(runuser -u omm -- bash -ec 'source /home/omm/.sse_opengauss_env; command -v "${CXX:-g++}"')
fi
export CXX
export LD_LIBRARY_PATH="$(dirname -- "$("$CXX" -print-file-name=libstdc++.so)")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [[ -z $out ]]; then out=$(mktemp -d /var/tmp/zyc-acceptance.XXXXXXXX)
else mkdir -m 0700 -- "$out" || fail 'evidence directory must be new'; fi
out=$(cd -- "$out" && pwd -P)
printf 'ZYC_EVIDENCE_DIR=%s\n' "$out"
trap 'rc=$?; printf "ZYC_RESULT=FAIL exit=%s evidence=%s\n" "$rc" "$out" >&2; exit "$rc"' ERR
export OPENGAUSS_PROJECT_ROOT="$repo"
python3 "$here/test_zyc_delivery.py" > "$out/entry-regressions.log" 2>&1
bash "$repo/team_week/liu_zhuoming/LZM-03/preflight/verify-all.sh" > "$out/baseline.log" 2>&1
baseline=$(sed -n 's/^RELEASE_EVIDENCE_DIR=//p' "$out/baseline.log")
[[ -n $baseline && -f $baseline/release.json ]]
cp "$baseline/release.json" "$out/baseline-release.json"
bash "$repo/team_week/liu_zhuoming/LZM-01/isolated.sh" init --port "$port" \
  < "$here/zyc_session.sh" > "$out/zyc-live.log" 2>&1
live=$(sed -n 's/^RUN_DIR=//p' "$out/zyc-live.log" | head -n1)
[[ -n $live && ! -e $live/db/instance/postmaster.pid ]]
mkdir "$out/requests"
cp "$live/evidence/live/"* "$out/requests/"
python3 "$here/zyc_evidence.py" "$repo" "$out"
grep -E '^(PASS ZYC|ADDITIONAL_RESULT=|ISOLATED_SERVER_STOPPED)' "$out/zyc-live.log"
printf 'ZYC_RESULT=PASS MODULE_CASES=18 BASELINE_LIVE_CASES=18 ZYC_CASES=13\n'
