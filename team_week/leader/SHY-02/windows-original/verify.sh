#!/bin/bash
set -euo pipefail
out=$(cd "$(dirname "$0")" && pwd)
repo=/opt/og/openGauss-server
if [[ ${1:-} != inside ]]; then
 export SHY02_NS=$(readlink /proc/self/ns/mnt)
 exec unshare -m --propagation private bash "$0" inside
fi
[[ "$(readlink /proc/self/ns/mnt)" != "$SHY02_NS" ]]
bench=$(mktemp -d /opt/og/shy02-check.XXXXXXXX)
echo "$bench" > "$out/evidence/runtime-path.txt"
chmod 755 "$bench"
mkdir "$bench/client" "$bench/server" "$bench/socket"
chown -R omm:dbgrp "$bench"
mount --make-rprivate /
sourcecopy=$(cat "$out/../SHY-01/evidence/runtime-path.txt")/source
mount --bind "$sourcecopy" "$repo"
mount --bind "$bench/client" "$repo/sse_storage"
mount --bind "$bench/server" "$repo/sse_server_storage"
as_omm() { runuser -u omm -- bash -c 'source /home/omm/.sse_opengauss_env; cd /opt/og/openGauss-server; exec "$@"' _ "$@"; }
as_omm bash -c '"$CXX" -std=c++11 -Wall -Wextra -Werror -I "$1/include" "$1/checks/header_check.cpp" -o "$2/header_check"; "$2/header_check"' _ "$out" "$bench" > "$out/evidence/header-check.log" 2>&1
echo HEADER_CHECK_PASS >> "$out/evidence/header-check.log"
as_omm bash -c '"$CXX" -std=c++11 -Wall -Wextra "$1/checks/crypto_probe.cpp" -I /opt/og/binarylibs/dependency/centos7.6_x86_64/openssl/comm/include -L /opt/og/openGauss-server/dest/lib -lcrypto -o "$2/crypto_probe"; "$2/crypto_probe"' _ "$out" "$bench" > "$out/evidence/crypto-probe.log" 2>&1
as_omm ldd "$bench/crypto_probe" > "$out/evidence/crypto-ldd.txt"
! grep -q 'not found' "$out/evidence/crypto-ldd.txt"
stop() { if [[ -f "$bench/db/postmaster.pid" ]]; then as_omm dest/bin/gs_ctl stop -D "$bench/db" -m fast > "$out/evidence/stop.log" 2>&1; fi; }
trap stop EXIT
as_omm dest/bin/gs_initdb -D "$bench/db" --nodename=shy02 --no-locale -E UTF8 --auth-local=trust --auth-host=reject > "$out/evidence/initdb.log" 2>&1
as_omm dest/bin/gs_ctl start -D "$bench/db" -Z single_node -l "$bench/server.log" -o "-p 55442 -c listen_addresses= -k $bench/socket" > "$out/evidence/start.log" 2>&1
gq() { as_omm dest/bin/gsql -X -h "$bench/socket" -p 55442 -d postgres -v ON_ERROR_STOP=1 "$@"; }
printf "ALTER ROLE omm PASSWORD 'Aa1!%s';\n" "$(openssl rand -hex 10)" | gq > "$bench/password.log" 2>&1
gq -a -f "$out/sql/physical_schema_draft.sql" > "$out/evidence/sql-draft.log" 2>&1
printf 'PASS_HEADER=yes\nPASS_CRYPTO_LIBRARY=yes\nPASS_PHYSICAL_SQL=yes\n' > "$out/evidence/check-summary.txt"
