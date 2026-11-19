#!/bin/bash
set -euo pipefail
out=$(cd "$(dirname "$0")" && pwd)
repo=/opt/og/openGauss-server
cd "$repo"
git rev-parse HEAD > "$out/evidence/source-commit.txt"
git status --short --untracked-files=no > "$out/evidence/original-status-before.txt"
git diff --binary HEAD > "$out/evidence/original-working.diff"
git diff --stat a3567e4151709fb0c1ffb7e9a8cb36d07d5c257f 25489f5 > "$out/evidence/commit-delta-stat.txt"
git diff a3567e4151709fb0c1ffb7e9a8cb36d07d5c257f 25489f5 -- src/common/interfaces/libpq/sse_common.cpp src/common/interfaces/libpq/fe-exec.cpp src/bin/psql/common.cpp src/bin/psql/startup.cpp src/common/interfaces/libpq/Client_Sophos.cpp sse_sql/SSE_init.sql > "$out/evidence/commit-delta-core.diff"
git show -s --format=fuller a3567e4 > "$out/evidence/a3567e4.txt"
bench=$(mktemp -d /opt/og/shy01-20261117.XXXXXXXX)
printf '%s\n' "$bench" > "$out/evidence/runtime-path.txt"
chmod 755 "$bench"
cp -a --reflink=auto "$repo" "$bench/source"
mkdir "$bench/client" "$bench/server" "$bench/socket"
chown -R omm:dbgrp "$bench"
for part in sse_storage sse_server_storage; do
 find "$repo/$part" -type f -printf '%P %s %T@\n' | sort
 done > "$out/evidence/original-state-before.txt"
cd "$bench/source"
git diff --quiet HEAD -- src/common/interfaces/libpq src/bin/psql sse_sql src/gausskernel/process/tcop
printf 'SOURCE_COPY=%s/source\n' "$bench"
printf 'SOURCE_CORE_MATCHES_HEAD=yes\n'
du -sh "$bench/source"
