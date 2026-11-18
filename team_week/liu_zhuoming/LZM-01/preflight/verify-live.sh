#!/usr/bin/env bash
# Run the public sample from a new isolated instance; logs remain local.
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
snapshot() {
  local dir
  for dir; do
    (
      cd "$dir"
      LC_ALL=C find . -printf '%y %m %u:%g %P\n' | LC_ALL=C sort
      find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum
    )
  done
}

expect_exit() {
  local case_id=$1 expected=$2 actual=$3
  if [[ $actual != "$expected" ]]; then
    printf 'FAIL %s: expected exit %s, got %s\n' "$case_id" "$expected" "$actual" >&2
    return 1
  fi
}

if [[ ${1:-} == --checks ]]; then
  evidence="$ISOLATED_RUN_DIR/evidence"
  snapshot "$ISOLATED_RUN_DIR/client" "$ISOLATED_RUN_DIR/server" > "$evidence/preflight.before"
  rc=0
  isolated.sh preflight --table team_fides_demo --keyword teamdemo20261012apple || rc=$?
  expect_exit LIVE-02 0 "$rc"
  echo 'PASS LIVE-02: registered table and keyword, exit=0'
  rc=0
  isolated.sh preflight --table team_fides_demo --keyword teamdemo20261012unlisted || rc=$?
  expect_exit LIVE-03 1 "$rc"
  echo 'PASS LIVE-03: unregistered keyword, exit=1'
  rc=0
  isolated.sh preflight --table team_fides_demo > "$evidence/argument.stdout" \
    2> "$evidence/argument.stderr" || rc=$?
  expect_exit LIVE-04 2 "$rc"
  if [[ -s $evidence/argument.stdout ]] || ! grep -q -- '--keyword' "$evidence/argument.stderr"; then
    echo 'FAIL LIVE-04: expected the missing keyword diagnostic on stderr only' >&2
    exit 1
  fi
  cat "$evidence/argument.stderr" >&2
  echo 'PASS LIVE-04: missing --keyword, exit=2'
  snapshot "$ISOLATED_RUN_DIR/client" "$ISOLATED_RUN_DIR/server" > "$evidence/preflight.after"
  if ! cmp "$evidence/preflight.before" "$evidence/preflight.after"; then
    echo 'FAIL LIVE-05: isolated state changed' >&2
    exit 1
  fi
  echo 'PASS LIVE-05: isolated state snapshots match'
  exit
fi

[[ $EUID == 0 ]] || { echo 'Run from the root shell.' >&2; exit 2; }
entry="$here/../isolated.sh"
out=$(mktemp -d /var/tmp/fides-isolation-check.XXXXXXXX)
echo "EVIDENCE_DIR=$out"
status=0

# Requires no other experiment modifying the original state during this test.
outer=(/opt/og/openGauss-server/sse_storage /opt/og/openGauss-server/sse_server_storage)
snapshot "${outer[@]}" > "$out/outer.before"
bash "$entry" init > "$out/init.log" 2>&1 <<'SESSION'
isolated.sh gsql -f "$(dirname "$ISOLATED_ENTRY")/preflight/seed.sql"
isolated.sh gsql -c "SELECT inet_server_addr() IS NULL AS unix_only, COALESCE(length(current_setting('listen_addresses')), 0) = 0 AS tcp_disabled;"
bash "$(dirname "$ISOLATED_ENTRY")/preflight/verify-live.sh" --checks
isolated.sh stop
SESSION
run=$(sed -n 's/^RUN_DIR=//p' "$out/init.log" | head -1)
[[ $run =~ ^/var/tmp/fides-isolated[.][a-zA-Z0-9]{8}$ ]]
grep -q '^PREFLIGHT_EXIT=3 ' "$out/init.log"
grep -Eq '^[[:space:]]*t[[:space:]]*\|[[:space:]]*t[[:space:]]*$' "$out/init.log"
grep -q '^ISOLATED_SERVER_STOPPED$' "$out/init.log"
[[ ! -e $run/db/instance/postmaster.pid ]]
echo 'PASS LIVE-01: empty state, exit=3; Unix socket ready and TCP disabled'
grep -E '^PASS LIVE-0[2-5]:' "$out/init.log"

# Save key/counter/map digests only in the private test directory.
fides_files=(client/fides_client.mat client/sse_table.mat client/tdp_sk.key
             client/derivation_master.key client/rsa_prg.key server/tdp_pk.key)
(cd "$run" && sha256sum "${fides_files[@]}") > "$out/stopped.sha"
install -m 0600 -o omm -g dbgrp "$out/stopped.sha" "$run/evidence/stopped.sha"
args=(--table team_fides_demo --keyword teamdemo20261012apple)
bash "$entry" resume "$run" "${args[@]}" > "$out/resume.log" 2>&1 <<'SESSION'
sha256sum -c evidence/stopped.sha > evidence/resume-snapshot.log
isolated.sh gsql -f "$(dirname "$ISOLATED_ENTRY")/preflight/resume.sql"
isolated.sh preflight --table team_fides_demo --keyword teamdemo20261012apple
SESSION
grep -q '^PREFLIGHT_EXIT=0 ' "$out/resume.log"
grep -Eq '"count": 2([,}])' "$out/init.log"
grep -Eq '"count": 4([,}])' "$out/resume.log"
awk -F'|' '
  $1 ~ /^[[:space:]]*[12][[:space:]]*$/ && $2 ~ /teamdemo20261012apple/ {
    gsub(/[[:space:]]/, "", $1); seen[$1]++
  }
  END {exit !(seen[1] == 1 && seen[2] == 1)}
' "$out/resume.log"
grep -q '^ISOLATED_SERVER_STOPPED$' "$out/resume.log"
[[ ! -e $run/db/instance/postmaster.pid ]]
echo 'PASS LIVE-06: same state on resume; apple IDs 1 and 2 once each, counter 2 -> 4'

for fault in missing truncated missing-key; do
  # Clone the stopped set; never alter the original state or DB.
  broken=$(mktemp -d /var/tmp/fides-isolated.XXXXXXXX)
  chmod 0711 "$broken"
  cp -a "$run"/{manifest,db,client,server,private,evidence,socket} "$broken/"
  case "$fault" in
    missing) case_id=LIVE-07; mv -- "$broken/client/fides_client.mat" "$broken/client/fides_client.saved" ;;
    truncated) case_id=LIVE-08; printf 'teamdemo20261012apple 4' > "$broken/client/fides_client.mat" ;;
    missing-key) case_id=LIVE-09; mv -- "$broken/client/tdp_sk.key" "$broken/client/tdp_sk.saved" ;;
  esac
  snapshot "$broken/client" "$broken/server" > "$out/$fault.before"
  rc=0
  bash "$entry" resume "$broken" "${args[@]}" </dev/null > "$out/$fault.log" 2>&1 || rc=$?
  [[ ! -e $broken/db/instance/postmaster.pid && ! -e $broken/start.log ]]
  expected=3
  if [[ $fault == missing-key ]]; then
    expected=4
    grep -q 'Missing, unreadable or empty key file:' "$out/$fault.log"
  fi
  expect_exit "$case_id" "$expected" "$rc"
  snapshot "$broken/client" "$broken/server" > "$out/$fault.after"
  cmp "$out/$fault.before" "$out/$fault.after"
  echo "PASS $case_id: $fault, exit=$rc; state snapshots match"
done

bash "$entry" resume "$run" "${args[@]}" > "$out/exit-failure.log" 2>&1 <<<'exit 29' || status=$?
[[ $status == 29 && ! -e $run/db/instance/postmaster.pid ]]
grep -q '^ISOLATED_SERVER_STOPPED$' "$out/exit-failure.log"
snapshot "${outer[@]}" > "$out/outer.after"
cmp "$out/outer.before" "$out/outer.after"
echo 'PASS LIVE-10: exit=29 preserved and instance stopped'
echo 'PASS: original state directory snapshots match'
