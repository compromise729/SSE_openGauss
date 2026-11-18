#!/bin/bash
set -euo pipefail
out=$(cd "$(dirname "$0")" && pwd)
bench=$(cat "$out/evidence/runtime-path.txt")
repo=/opt/og/openGauss-server
if [[ ${1:-} != inside ]]; then
 export SHY_OUTER_NS=$(readlink /proc/self/ns/mnt)
 exec unshare -m --propagation private bash "$0" inside
fi
test "$(readlink /proc/self/ns/mnt)" != "$SHY_OUTER_NS"
mount --make-rprivate /
mount --bind "$bench/source" "$repo"
mount --bind "$bench/client" "$repo/sse_storage"
mount --bind "$bench/server" "$repo/sse_server_storage"
findmnt -T "$repo" > "$out/evidence/mounts.txt"
findmnt -T "$repo/sse_storage" >> "$out/evidence/mounts.txt"
findmnt -T "$repo/sse_server_storage" >> "$out/evidence/mounts.txt"
as_omm() {
 runuser -u omm -- bash -c 'source /home/omm/.sse_opengauss_env; cd /opt/og/openGauss-server; exec "$@"' _ "$@"
}
cd "$repo"
for patchfile in "$out"/patches/*.patch; do
 printf '\nPATCH=%s\n' "$patchfile"
 patch --dry-run -p1 < "$patchfile"
done > "$out/evidence/patch-dry-run.log" 2>&1
# Rebuild the implementation files exercised by the baseline from the isolated source.
as_omm touch src/bin/psql/common.cpp src/bin/psql/startup.cpp src/common/interfaces/libpq/fe-exec.cpp src/common/interfaces/libpq/sse_common.cpp src/common/interfaces/libpq/Client_Sophos.cpp src/gausskernel/process/tcop/postgres.cpp src/gausskernel/process/tcop/Server_Sophos.cpp
as_omm make -j4 > "$out/evidence/build.log" 2>&1
printf 'BUILD_EXIT=0\n'
as_omm make install > "$out/evidence/install.log" 2>&1
printf 'INSTALL_EXIT=0\n'
for f in dest/bin/gsql dest/bin/gaussdb dest/lib/libpq_ce.so.5.5 dest/lib/libsse_crypto.so dest/lib/libmitra_sse_crypto.so; do sha256sum "$f"; done > "$out/evidence/binary-sha256.txt"
as_omm ldd dest/bin/gsql > "$out/evidence/gsql-ldd.txt"
as_omm ldd dest/bin/gaussdb > "$out/evidence/gaussdb-ldd.txt"
! grep -q 'not found' "$out/evidence/"*-ldd.txt
as_omm dest/bin/gsql --version > "$out/evidence/gsql-version.txt" 2>&1
as_omm dest/bin/gaussdb --version > "$out/evidence/gaussdb-version.txt" 2>&1
stop() {
 if test -f "$bench/db/postmaster.pid"; then as_omm dest/bin/gs_ctl stop -D "$bench/db" -m fast > "$out/evidence/stop.log" 2>&1; fi
}
trap stop EXIT
as_omm dest/bin/gs_initdb -D "$bench/db" --nodename=shy01 --no-locale -E UTF8 --auth-local=trust --auth-host=reject > "$out/evidence/initdb.log" 2>&1
as_omm dest/bin/gs_ctl start -D "$bench/db" -Z single_node -l "$bench/server.log" -o "-p 55441 -c listen_addresses= -k $bench/socket" > "$out/evidence/start.log" 2>&1
gq() { as_omm dest/bin/gsql -X -h "$bench/socket" -p 55441 -d postgres -v ON_ERROR_STOP=1 "$@"; }
printf "ALTER ROLE omm PASSWORD 'Aa1!%s';\n" "$(openssl rand -hex 10)" | gq > "$bench/password-setup.log" 2>&1
printf 'ROLE_INITIALIZED=yes\n'
gq -f sse_sql/SSE_init.sql > "$out/evidence/sql-init.log" 2>&1
for testname in 01-write 02-search 03-reconnect; do
 gq -a -f "$out/sql/$testname.sql" > "$out/evidence/$testname.log" 2>&1
 cp "$bench/client/fides_client.mat" "$out/evidence/$testname-counter.txt"
 echo "SMOKE_${testname}=0"
done
gq -t -A -F '|' -c 'SELECT ind, keyword, payload FROM shy01_demo_data ORDER BY ind;' > "$out/evidence/physical.tsv" 2>&1
printf 'SMOKE_COMPLETED=yes\n'
