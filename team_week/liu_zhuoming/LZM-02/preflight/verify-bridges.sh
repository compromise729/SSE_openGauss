#!/usr/bin/env bash
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "${OPENGAUSS_PROJECT_ROOT:-$here/../../../..}" && pwd -P)
out=$(mktemp -d /tmp/lzm02-bridges.XXXXXXXX)
echo "EVIDENCE_DIR=$out"
"${CXX:-g++}" -std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0 -pthread -Wall -Wextra -Werror \
  -I"$repo/src/include" "$here/bridge-cases.cpp" \
  -L"$repo/src/common/interfaces/libpq" -Wl,-rpath,"$repo/src/common/interfaces/libpq" \
  -L"$repo/dest/lib" -Wl,-rpath,"$repo/dest/lib" -lpq_ce -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp -o "$out/bridge-cases"
chmod 0711 "$out"
chmod 0755 "$out/bridge-cases"
for mode in append whole-map global-map counter-failure map-failure; do
  if (( EUID == 0 )); then
    install -d -m 0700 -o omm -g dbgrp "$out/$mode"
    runuser -u omm -- env LD_LIBRARY_PATH="$repo/src/common/interfaces/libpq:${LD_LIBRARY_PATH:-}" \
      "$out/bridge-cases" "$out/$mode" "$mode"
  else
    mkdir "$out/$mode"
    LD_LIBRARY_PATH="$repo/src/common/interfaces/libpq:${LD_LIBRARY_PATH:-}" \
      "$out/bridge-cases" "$out/$mode" "$mode"
  fi
done
