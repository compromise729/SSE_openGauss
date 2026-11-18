#!/bin/bash
set -euo pipefail
out=$(cd "$(dirname "$0")" && pwd)
bench=$(cat "$out/evidence/runtime-path.txt")
repo=/opt/og/openGauss-server
cd "$repo"
git status --short --untracked-files=no > "$out/evidence/original-status-after.txt"
for part in sse_storage sse_server_storage; do
 find "$repo/$part" -type f -printf '%P %s %T@\n' | sort
done > "$out/evidence/original-state-after.txt"
cmp "$out/evidence/original-state-before.txt" "$out/evidence/original-state-after.txt"
cmp "$out/evidence/original-status-before.txt" "$out/evidence/original-status-after.txt"
sha256sum -c "$out/evidence/original-core-before.sha256" > "$out/evidence/original-core-after-check.txt"
sha256sum -c "$out/evidence/original-binary-before.sha256" > "$out/evidence/original-binary-after-check.txt"
# Discard only inherited copies of old state inside this task-owned source copy.
resolved=$(readlink -f "$bench/source")
[[ "$resolved" == /opt/og/shy01-20261117.*/source ]]
[[ "$resolved" != "$repo" ]]
! test -f "$bench/db/postmaster.pid"
find "$resolved/sse_storage" "$resolved/sse_server_storage" -maxdepth 1 -type f -delete
chmod 700 "$bench/client" "$bench/server" "$bench/db"
cd "$resolved"
git diff --quiet HEAD -- src/common/interfaces/libpq src/bin/psql sse_sql src/gausskernel/process/tcop
git status --short --untracked-files=no > "$out/evidence/copy-status.txt"
sha256sum src/bin/psql/common.o src/common/interfaces/libpq/fe-exec.o src/common/interfaces/libpq/sse_common.o src/gausskernel/process/tcop/postgres.o > "$out/evidence/rebuilt-object-sha256.txt"
stat -c '%y %s %n' dest/bin/gsql dest/bin/gaussdb dest/lib/libpq_ce.so.5.5 > "$out/evidence/build-artifact-stat.txt"
printf 'ORIGINAL_CORE_UNCHANGED=yes\nORIGINAL_BINARIES_UNCHANGED=yes\nORIGINAL_STATE_UNCHANGED=yes\nISOLATED_SERVER_STOPPED=yes\nBASELINE_SOURCE_CORE_CLEAN=yes\n' > "$out/evidence/final-check.txt"
