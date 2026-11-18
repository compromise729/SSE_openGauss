#!/usr/bin/env bash
# Test ciphertext storage against a fresh openGauss instance using public fixtures.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
: "${GAUSSHOME:?set GAUSSHOME to the installed openGauss prefix}"
: "${SSE_LEGACY_ROOT:?set SSE_LEGACY_ROOT to the fixed state root of the old binary}"
db_user=${YZH_FIXTURE_DB_USER:-omm}
[[ "$db_user" =~ ^[a-z_][a-z0-9_]*$ ]]
if [[ "${1:-}" != inside ]]; then
  [[ "$(id -u)" == 0 ]]
  export YZH_PARENT_MNT=$(readlink /proc/self/ns/mnt)
  exec unshare -m --propagation private bash "$0" inside
fi
[[ "$(readlink /proc/self/ns/mnt)" != "${YZH_PARENT_MNT:?run without arguments}" ]]
export LD_LIBRARY_PATH="$GAUSSHOME/lib:${LD_LIBRARY_PATH:-}"
export PATH="$GAUSSHOME/bin:$PATH"
scratch=$(mktemp -d /tmp/yzh01-db.XXXXXXXX)
port=55452
as_db() { runuser -u "$db_user" -- "$@"; }
finish() {
  if [[ -f "$scratch/db/postmaster.pid" ]]; then
    as_db gs_ctl stop -D "$scratch/db" -m fast >"$scratch/stop.log" 2>&1 || true
  fi
  if [[ -f "$scratch/db/postmaster.pid" ]]; then
    printf 'Database stop failed. Fixture preserved at %s\n' "$scratch" >&2
    return 1
  fi
  rm -rf -- "$scratch"
}
trap finish EXIT
chmod 0755 "$scratch"
install -d -m 0700 -o "$db_user" "$scratch/client" "$scratch/server" "$scratch/socket" "$scratch/db"
mount --bind "$scratch/client" "$SSE_LEGACY_ROOT/sse_storage"
mount --bind "$scratch/server" "$SSE_LEGACY_ROOT/sse_server_storage"
as_db gs_initdb -D "$scratch/db" --nodename=yzh_fixture --no-locale -E UTF8 \
  --auth-local=trust --auth-host=reject >"$scratch/init.log" 2>&1 || { cat "$scratch/init.log"; exit 1; }
as_db gs_ctl start -D "$scratch/db" -Z single_node -l "$scratch/db/server.log" \
  -o "-p $port -c listen_addresses= -k $scratch/socket" >"$scratch/start.log" 2>&1 ||
  { cat "$scratch/start.log"; tail -35 "$scratch/db/server.log"; exit 1; }
gq() {
  as_db gsql -X -h "$scratch/socket" -p "$port" -d postgres -v ON_ERROR_STOP=1 -qAt -F $'\t' "$@"
}
# New blank local cluster requires a login password even with Unix socket auth.
password=$(openssl rand -hex 14)
printf "ALTER ROLE %s PASSWORD 'Aa1!%s';\n" "$db_user" "$password" | gq >/dev/null
unset password
YZH_FIXTURE_DB_OID=$(gq -c 'SELECT oid::text FROM pg_database WHERE datname=current_database()' | grep -E '^[0-9]+$')
export YZH_FIXTURE_DB_OID
"$here/build/test_crypto" "$scratch" "$scratch/expected.tsv" --sql-write >"$scratch/fixture.sql"
if grep -Eq "red apple|green apple|yellow banana|rid\\|add" "$scratch/fixture.sql"; then
  printf 'FAIL fixture SQL contained plaintext\n' >&2
  exit 1
fi
gq <"$scratch/fixture.sql" | grep -E '^YZH:[dl]' >"$scratch/readback.tsv"
cmp "$scratch/expected.tsv" "$scratch/readback.tsv"
printf 'PASS SQL write/read stored exactly three row blobs and three locator blobs (one resealed)\n'
"$here/build/test_crypto" "$scratch" "$scratch/readback.tsv" --sql-read
as_db gs_ctl stop -D "$scratch/db" -m fast >"$scratch/stop.log" 2>&1
printf 'PASS isolated openGauss fixture stopped\n'
