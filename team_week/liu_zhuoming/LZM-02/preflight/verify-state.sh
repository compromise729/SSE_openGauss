#!/usr/bin/env bash
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "${OPENGAUSS_PROJECT_ROOT:-$here/../../../..}" && pwd -P)
out=$(mktemp -d /tmp/lzm02-state.XXXXXXXX)
echo "EVIDENCE_DIR=$out"
"${CXX:-g++}" -std=c++11 -pthread -Wall -Wextra -Werror -I"$repo/src/include" \
  "$here/state-cases.cpp" "$repo/src/common/interfaces/libpq/row_state_v1.cpp" \
  -Wl,--wrap=renameat,--wrap=fsync,--wrap=unlinkat -o "$out/state-cases"
chmod 0711 "$out"
chmod 0755 "$out/state-cases"
if ((EUID == 0)); then
    install -d -m 0700 -o omm -g dbgrp "$out/cases"
    run=(runuser -u omm --)
else
    mkdir "$out/cases"
    run=(env)
fi
"${run[@]}" "$out/state-cases" "$out/cases"
for fault in map save unlink clear-sync staged; do
    "${run[@]}" "$out/state-cases" "$out/cases" "$fault"
done
