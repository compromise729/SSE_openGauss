#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "${OPENGAUSS_PROJECT_ROOT:-$here/../../../..}" && pwd -P)
[[ $# == 1 ]] || { echo 'Usage: build-checker.sh OUTPUT_PATH' >&2; exit 2; }
out=$(cd -- "$(dirname -- "$1")" && pwd -P)
bash "$here/build-state-module.sh" "$out"
"${CXX:-g++}" -std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0 -pthread -Wall -Wextra -Werror -I"$repo/src/include" \
  "$here/check-state.cpp" \
  -L"$out" -Wl,-rpath,"$out" -lstate_verify \
  -L"$repo/dest/lib" -Wl,-rpath,"$repo/dest/lib" -lpq_ce -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp -o "$1"
