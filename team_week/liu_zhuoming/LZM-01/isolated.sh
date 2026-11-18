#!/usr/bin/env bash
set -euo pipefail
umask 077
ulimit -c 0

self=$(readlink -f -- "$0")
here=$(dirname -- "$self")
# Use the project root when the delivery is outside the source tree.
if [[ -n ${OPENGAUSS_PROJECT_ROOT:-} ]]; then
  repo=$OPENGAUSS_PROJECT_ROOT
elif [[ -f sse_sql/SSE_init.sql ]]; then
  repo=$(pwd -P)
else
  repo=$(cd -- "$here/../../.." && pwd -P)
fi
prep="$here/preflight"
dest="$repo/dest"
runtime=/home/omm/.sse_opengauss_env
client_target=/opt/og/openGauss-server/sse_storage
server_target=/opt/og/openGauss-server/sse_server_storage

die() { printf 'Fides isolation: %s\n' "$*" >&2; exit 4; }
usage() {
  echo "Usage: $self init [--port PORT] | resume RUN_DIR --table TABLE --keyword KEYWORD" >&2
  echo "In the isolated session: isolated.sh preflight --table TABLE --keyword KEYWORD | gsql [arguments] | stop" >&2
  exit 2
}

# Only omm reads its runtime profile. The supervisor never sources user files.
as_omm() {
  runuser -u omm -- env -i PATH=/usr/bin:/bin HOME=/home/omm USER=omm LOGNAME=omm \
    TERM="${TERM:-dumb}" ISOLATED_RUN_DIR="${run:-}" ISOLATED_PORT="${port:-}" \
    ISOLATED_MOUNT_NAMESPACE="${ns:-}" OPENGAUSS_PROJECT_ROOT="$repo" ISOLATED_ENTRY="$self" \
    bash -ec '
      prefix=$1; profile=$2; tool_dir=$3; shift 3
      source "$profile"
      export GAUSSHOME="$prefix" PATH="$tool_dir:$prefix/bin:$PATH"
      export LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
      export PGCLIENTENCODING=UTF8 PYTHONIOENCODING=utf-8
      exec "$@"
    ' _ "$dest" "$runtime" "$here" "$@" 9>&-
}

check_paths() {
  [[ -d $client_target && ! -L $client_target && -d $server_target && ! -L $server_target ]] ||
    die "compiled state mount targets are absent or symbolic links"
  mountpoint -q "$client_target" && mountpoint -q "$server_target" &&
    [[ $client_target -ef $run/client && $server_target -ef $run/server && ! $client_target -ef $server_target ]] ||
    die "client/server bind mounts do not match this run"
}

state_check() {
  python3 "$prep/check_fides_state.py" "$@" --counter-file "$run/client/fides_client.mat" \
    --mapping-file "$run/client/sse_table.mat"
}

# Guard for all commands exposed to the unprivileged session.
in_session() {
  run=${ISOLATED_RUN_DIR:?Use init or resume first}
  port=${ISOLATED_PORT:?Use init or resume first}
  [[ $(id -un) = omm && $(readlink /proc/self/ns/mnt) == "${ISOLATED_MOUNT_NAMESPACE:-}" ]] ||
    die "command must run as omm in its original isolated session"
  check_paths
}

stop_db() {
  local pid pgdir
  [[ -e $db/postmaster.pid ]] || return 0
  [[ -f $db/postmaster.pid && ! -L $db/postmaster.pid ]] || return 1
  pid=$(sed -n '1p' "$db/postmaster.pid")
  pgdir=$(sed -n '2p' "$db/postmaster.pid")
  # omm cannot read a namespace link when the postmaster is non-dumpable.
  # The root supervisor checks the namespace; the session checks the process cwd.
  [[ $pid =~ ^[1-9][0-9]*$ && $pgdir == "$db" ]] || return 1
  if (( EUID == 0 )); then
    [[ $(readlink "/proc/$pid/ns/mnt") == "$ns" ]] || return 1
    as_omm "$dest/bin/gs_ctl" stop -D "$db" -m fast -w
  else
    [[ $(readlink "/proc/$pid/cwd") == "$db" ]] || return 1
    "$dest/bin/gs_ctl" stop -D "$db" -m fast -w
  fi
  [[ ! -e $db/postmaster.pid ]]
}

check_installation() {
  local dependencies libpq actual_client actual_server
  [[ -f $repo/sse_sql/SSE_init.sql ]] ||
    die "run init/resume from the openGauss project root (missing sse_sql/SSE_init.sql)"
  [[ -r $runtime ]] || die "missing runtime profile $runtime"
  id omm >/dev/null && getent group dbgrp >/dev/null || die "requires omm and dbgrp"
  local name
  for name in gaussdb gsql gs_ctl gs_initdb pg_controldata; do
    [[ -x $dest/bin/$name ]] || die "missing $dest/bin/$name"
  done
  dependencies=$(as_omm ldd "$dest/bin/gsql")
  [[ $dependencies != *'not found'* ]] || die "gsql dependencies unresolved"
  libpq=$(awk '/^[[:blank:]]*libpq(_ce)?[.]so[.]/ && $2=="=>" {print $3}' <<< "$dependencies")
  [[ $libpq == "$dest/lib/"* && $libpq != *$'\n'* && -f $libpq ]] ||
    die "gsql is not linked to the selected installation's libpq"
  # Check the embedded Fides paths in the objects actually used. Any other
  # absolute storage root is an unsupported build and must be reviewed first.
  actual_client=$(LC_ALL=C strings "$dest/bin/gsql" "$libpq" |
    sed -nE 's@^(/[^[:space:]]*/sse_storage)(/[^[:space:]]*)?$@\1@p' | LC_ALL=C sort -u)
  actual_server=$(LC_ALL=C strings "$dest/bin/gaussdb" |
    sed -nE 's@^(/[^[:space:]]*/sse_server_storage)(/[^[:space:]]*)?$@\1@p' | LC_ALL=C sort -u)
  [[ $actual_client == "$client_target" && $actual_server == "$server_target" ]] ||
    die "cannot confirm the compiled Fides storage paths"
}

make_run() {
  run=$(mktemp -d /var/tmp/fides-isolated.XXXXXXXX)
  chmod 0711 "$run"
  install -d -m 0700 -o omm -g dbgrp "$run"/{client,server,db,socket,private,evidence}
  printf 'RUN_DIR=%s\n' "$run"
}

validate_layout() {
  local dir
  for dir in client server db socket private evidence; do
    [[ -d $run/$dir && ! -L $run/$dir && $(stat -c '%U:%a' -- "$run/$dir") == omm:700 ]] ||
      die "invalid run layout: $run/$dir"
  done
  db="$run/db/instance"
}

validate_stopped() {
  [[ -d $db && ! -L $db && -s $db/PG_VERSION && ! -L $db/PG_VERSION &&
     ! -e $db/postmaster.pid && ! -L $db/postmaster.pid ]] ||
    die "instance absent, active, or unclean; preserve the run for diagnosis"
  # A copied cluster/state set must not redirect writes back to another run.
  local linked
  linked=$(find "$db" "$run/client" "$run/server" \( -type l -o \( -type f -links +1 \) \) -print -quit)
  [[ -z $linked ]] || die "linked file in run: $linked"
  local control
  control=$(as_omm "$dest/bin/pg_controldata" "$db")
  grep -Eq '^Database cluster state:[[:space:]]+shut down$' <<< "$control" ||
    die "pg_controldata does not report a clean shutdown"
}

persisted_keys() {
  as_omm bash -ec '
    for file; do
      [[ -f $file && ! -L $file && -r $file && -s $file ]] ||
        { printf "Missing, unreadable or empty key file: %s\n" "$file" >&2; exit 4; }
    done
  ' _ "$run/client/tdp_sk.key" "$run/client/derivation_master.key" \
    "$run/client/rsa_prg.key" "$run/server/tdp_pk.key"
}

cleanup() {
  local status=$? stop_rc=0
  trap - EXIT
  if [[ ${start_attempted:-0} == 1 ]]; then
    stop_db >> "$run/stop.log" 2>&1 || stop_rc=$?
    if (( stop_rc == 0 )); then
      echo 'ISOLATED_SERVER_STOPPED'
    else
      printf 'Fides isolation: stop failed; preserve %s and inspect stop.log\n' "$run" >&2
      status=4
    fi
  fi
  exit "$status"
}

main() {
  local mode=$1; shift
  [[ $EUID == 0 ]] || die "init/resume require root inside Linux/WSL"
  ns=$(readlink /proc/self/ns/mnt)
  [[ $ns != "${OUTER_MOUNT_NAMESPACE:?missing outer namespace}" ]] || die "unshare did not create a private namespace"

  port=55432
  case $mode in
    init)
      if (( $# )); then [[ $# == 2 && $1 == --port ]] || usage; port=$2; fi
      ;;
    resume)
      [[ $# == 5 && $2 == --table && $4 == --keyword && -n $3 && -n $5 ]] || usage
      run=$1 table=$3 keyword=$5
      # Accept the previous directory prefix so existing experiments can resume.
      [[ $run =~ ^/var/tmp/(fides-isolated|lzm01)[.][a-zA-Z0-9]{8}$ && ! -L $run &&
         $(stat -c '%U:%a' -- "$run") == root:711 ]] || die "resume requires the original isolated RUN_DIR"
      ;;
    *) usage ;;
  esac

  check_installation
  if [[ $mode == init ]]; then
    make_run
  fi
  exec 9>"$run/lifecycle.lock"
  flock -n 9 || die "a lifecycle session is already using $run"
  validate_layout
  if [[ $mode == resume ]]; then
    [[ -f $run/manifest && ! -L $run/manifest && $(stat -c '%U:%a' "$run/manifest") == root:600 ]] ||
      die "missing or invalid run manifest"
    local -a manifest
    mapfile -t manifest < "$run/manifest"
    case ${#manifest[@]} in
      1) port=${manifest[0]} ;;
      # Older manifests contain the commit, binary hash and port.
      3) port=${manifest[2]} ;;
      *) die "invalid saved port in $run/manifest" ;;
    esac
    validate_stopped
  fi
  [[ $port =~ ^[1-9][0-9]{0,4}$ ]] && (( port <= 65535 )) || usage
  [[ ! -e $run/socket/.s.PGSQL.$port && ! -L $run/socket/.s.PGSQL.$port &&
     ! -e $run/socket/.s.PGSQL.$port.lock && ! -L $run/socket/.s.PGSQL.$port.lock ]] ||
    die "socket is already present; preserve the run for diagnosis"
  [[ $(realpath -- "$client_target") == "$client_target" && $(realpath -- "$server_target") == "$server_target" &&
     -d $client_target && -d $server_target && ! $client_target -ef $server_target ]] ||
    die "cannot safely use the compiled mount targets"
  mount --make-rprivate /
  mount --bind "$run/client" "$client_target"
  mount --bind "$run/server" "$server_target"
  check_paths

  local rc=0 phase=pre-init
  if [[ $mode == resume ]]; then phase=pre-resume; fi
  local record
  record=$(mktemp "$run/$phase.XXXXXXXX.json")
  if [[ $mode == init ]]; then
    table=team_fides_demo keyword=teamdemo20261012apple
  fi
  as_omm "$self" preflight --table "$table" --keyword "$keyword" > "$record" || rc=$?
  printf 'PREFLIGHT_EXIT=%s RECORD=%s\n' "$rc" "$record"
  cat "$record"
  if [[ $mode == init ]]; then
    [[ $rc == 3 && -z $(ls -A -- "$run/client") && -z $(ls -A -- "$run/server") ]] ||
      die "new state directories must be empty and the initial check must return 3"
    printf '%s\n' "$port" > "$run/manifest"
    as_omm "$dest/bin/gs_initdb" -D "$db" --nodename=fides_isolated --no-locale -E UTF8 \
      --auth-local=trust --auth-host=reject > "$run/init.log" 2>&1
  else
    [[ $rc == 0 ]] || exit "$rc"
    persisted_keys
  fi

  trap cleanup EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  trap 'exit 129' HUP
  start_attempted=1
  as_omm "$dest/bin/gs_ctl" start -D "$db" -Z single_node -w -l "$run/private/server.log" \
    -o "-p $port -c listen_addresses= -k $run/socket" > "$run/start.log" 2>&1 ||
    die "server start failed; see $run/start.log"
  [[ -S $run/socket/.s.PGSQL.$port ]] || die "expected Unix socket did not appear"
  if [[ $mode == init ]]; then
    # openGauss requires setting the bootstrap role password before other SQL.
    local secret
    secret=$(openssl rand -hex 12)
    printf "ALTER ROLE omm PASSWORD 'Aa1!%s';\n" "$secret" |
      as_omm "$self" gsql > "$run/bootstrap.log" 2>&1 ||
      die "bootstrap role setup failed; see $run/bootstrap.log"
    unset secret
  else
    # gs_ctl can report ready before backend initialization has finished.
    # Complete a plain SQL round-trip before allowing an immediate session exit.
    as_omm "$self" gsql -c 'SELECT 1;' > "$run/readiness.log" 2>&1 ||
      die "server readiness query failed; see $run/readiness.log"
  fi
  printf 'RUN_DIR=%s\nINSTANCE=%s\nSOCKET=%s\nPORT=%s\nCLIENT=%s (mounted at %s)\nSERVER=%s (mounted at %s)\nPID=%s\n' \
    "$run" "$db" "$run/socket" "$port" "$run/client" "$client_target" "$run/server" "$server_target" \
    "$(head -n1 "$db/postmaster.pid")"
  printf 'In this omm shell: isolated.sh gsql | preflight | stop; exit also stops the instance.\n'
  cd "$run"
  if [[ -t 0 ]]; then
    as_omm bash --noprofile --norc -i
  else
    as_omm bash --noprofile --norc -es
  fi
}

case ${1:-} in
  init|resume)
    [[ $EUID == 0 ]] || die "run the lifecycle entry as root inside Linux/WSL"
    export OUTER_MOUNT_NAMESPACE=$(readlink /proc/self/ns/mnt)
    exec unshare -m --propagation private bash "$self" __run "$@"
    ;;
  __run)
    shift
    main "$@"
    ;;
  preflight)
    shift
    in_session
    state_check "$@"
    ;;
  gsql)
    shift
    in_session
    # Only pass application SQL/output options; connection arguments remain fixed.
    case $# in
      0) ;;
      2) [[ $1 == -f || $1 == -c ]] || usage ;;
      *) usage ;;
    esac
    exec "$dest/bin/gsql" -X -h "$run/socket" -p "$port" -d postgres -U omm \
      -v ON_ERROR_STOP=1 -v "SSE_INIT_SQL=$repo/sse_sql/SSE_init.sql" "$@"
    ;;
  stop)
    [[ $# == 1 ]] || usage
    in_session
    db="$run/db/instance"
    stop_db || die "stop failed; exit the session for supervisor cleanup"
    ;;
  *) usage ;;
esac
