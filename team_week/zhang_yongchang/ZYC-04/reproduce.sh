#!/usr/bin/env bash
# ZYC-04 复跑入口：在独立 CentOS 7 root 终端运行。
# 只创建新证据目录，不清理或改写他人目录。
set -u
umask 077

repo=
out=
while (( $# )); do
  case "$1" in
    --repo) repo=$2; shift 2 ;;
    --evidence-dir) out=$2; shift 2 ;;
    --help) sed -n '2,5p' "$0"; exit 0 ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
  esac
done
[[ -n $repo && -d $repo ]] || { printf 'usage: %s --repo <checkout> [--evidence-dir <new dir>]\n' "$0" >&2; exit 2; }
repo=$(cd -- "$repo" && pwd -P)
if [[ -z $out ]]; then out=$(mktemp -d /var/tmp/zyc04-reproduce.XXXXXXXX); else mkdir -m 0700 -- "$out" || exit 2; fi
out=$(cd -- "$out" && pwd -P)
printf 'ZYC04_REPO=%s\nZYC04_EVIDENCE=%s\n' "$repo" "$out"

rc_all=0
run() {
  local name=$1; shift
  local log="$out/$name.log"
  printf '\n=== %s ===\n' "$name" | tee -a "$out/summary.log"
  set +e
  "$@" > "$log" 2>&1
  local rc=$?
  set -e 2>/dev/null || true
  printf '%s rc=%s log=%s\n' "$name" "$rc" "$log" | tee -a "$out/summary.log"
  [[ $rc -eq 0 ]] || rc_all=1
}

mkdir -p "$out/zyc" "$out/yzh02" "$out/lmd03" "$out/lzm04"

# 1. ZYC 正常闭环 + 13 项实库夹具（含追加 rid、空字段、末行认证失败、缺行业务行、wire 阶段）
run zyc bash "$repo/team_week/zhang_yongchang/tools/zyc_live_verify_all.sh" \
  --project-root "$repo" --evidence-dir "$out/zyc/evidence" --port 55443

# 2. YZH-02 真实客户端故障矩阵（18 项）
run yzh02 env OPENGAUSS_PROJECT_ROOT="$repo" \
  bash -c "bash \"$repo/team_week/liu_zhuoming/LZM-01/isolated.sh\" init --port 55444 < \"$repo/team_week/yan_zihan/YZH-02/yzh02_session.sh\""

# 3. LMD-03 数据库一致性与失败原子性（11 项）
run lmd03 bash "$repo/team_week/li_mengde/LMD-03/run.sh" \
  --project-root "$repo" --evidence-dir "$out/lmd03/evidence"

# 4. LZM-04 状态、中断、重启（36 项）
run lzm04 bash -c '
  set -euo pipefail
  source /home/omm/.sse_opengauss_env
  source /opt/rh/rh-python36/enable
  tools="$1/team_week/liu_zhuoming/LZM-04/preflight"
  build="$2/lzm04-build"
  mkdir -m 0700 "$build"
  python3 "$tools/build-current.py" --repo "$1" --out "$build"
  bash "$tools/verify.sh" "$build"
' _ "$repo" "$out/lzm04"

printf 'ZYC04_REPRODUCE_EXIT=%s\n' "$rc_all" | tee -a "$out/summary.log"
exit "$rc_all"
