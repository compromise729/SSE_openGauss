#!/bin/bash
set -euo pipefail
out=$(cd "$(dirname "$0")" && pwd)
cp /tmp/fides-preflight.TENvsma0/executed "$out/evidence/lzm-offline-executed.txt"
cp /tmp/fides-preflight.TENvsma0/argument.stdout "$out/evidence/lzm-argument.stdout"
cp /tmp/fides-preflight.TENvsma0/argument.stderr "$out/evidence/lzm-argument.stderr"
cp "$out/../../liu_zhuoming/LZM-01/preflight/result/live.log" "$out/evidence/lzm-member-live.log"
release=$(mktemp -d /opt/og/shy02-release.XXXXXXXX)
chown omm:dbgrp "$release"
runuser -u omm -- git clone --shared --no-checkout /opt/og/openGauss-server "$release/source" > "$out/evidence/release-clone.log" 2>&1
runuser -u omm -- bash -ec 'cd "$1/source"; git checkout -b team/shy02-contract-v1 25489f563f25b522d0cd37a6c85865039e3f340f; mkdir -p team_week/leader/SHY-02; cp -r "$2/include" "$2/sql" "$2/checks" team_week/leader/SHY-02/; cp "$2/接口与数据格式约定.md" "$2/验收样例与预期结果.md" "$2/四人接续实现清单.md" team_week/leader/SHY-02/; git add team_week/leader/SHY-02; git -c user.name="Shi Huayang" -c user.email="2604082080@qq.com" commit -m "docs: freeze SHY-02 Fides row encryption contract v1"; git rev-parse HEAD' _ "$release" "$out" > "$out/evidence/release-commit.log" 2>&1
cd "$release/source"
git rev-parse HEAD > "$out/evidence/release-commit.txt"
printf '%s/source\n' "$release" > "$out/evidence/release-path.txt"
git show --stat --oneline HEAD > "$out/evidence/release-stat.txt"
git diff --exit-code HEAD^ HEAD -- src sse_sql > "$out/evidence/source-change-check.txt"
