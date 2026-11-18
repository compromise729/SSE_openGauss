#!/bin/bash
set -euo pipefail
out=$(cd "$(dirname "$0")" && pwd)
cd /opt/og/openGauss-server
paths=(src/common/interfaces/libpq/sse_common.cpp src/common/interfaces/libpq/fe-exec.cpp src/bin/psql/common.cpp src/bin/psql/startup.cpp src/common/interfaces/libpq/Client_Sophos.cpp src/common/interfaces/libpq/SSE_map.cpp src/gausskernel/process/tcop/postgres.cpp src/gausskernel/process/tcop/Server_Sophos.cpp sse_sql/SSE_init.sql)
for rev in 25489f5 a3567e4; do
 for p in "${paths[@]}"; do printf '%s %s ' "$rev" "$p"; git show "$rev:$p" | sha256sum | cut -d ' ' -f1; done
done > "$out/evidence/commit-core-sha256.txt"
for p in "${paths[@]}"; do sha256sum "$p"; done > "$out/evidence/original-core-before.sha256"
sha256sum dest/bin/gsql dest/bin/gaussdb dest/lib/libpq_ce.so.5.5 > "$out/evidence/original-binary-before.sha256"
git diff --stat HEAD > "$out/evidence/original-diff-stat.txt"
