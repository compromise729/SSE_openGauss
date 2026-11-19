#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "${OPENGAUSS_PROJECT_ROOT:-$here/../../../..}" && pwd -P)
[[ $# == 1 ]] || { echo 'Usage: build-checker.sh OUTPUT_PATH' >&2; exit 2; }
src="$repo/src/common/interfaces/libpq"
"${CXX:-g++}" -std=c++11 -pthread -Wall -Wextra -Werror -I"$repo/src/include" \
  "$here/check-state.cpp" "$src/row_state_v1.cpp" "$src/row_state_catalog.cpp" \
  -L"$repo/dest/lib" -Wl,-rpath,"$repo/dest/lib" -lpq_ce -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp -o "$1"
