#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "${OPENGAUSS_PROJECT_ROOT:-$here/../../../..}" && pwd -P)
[[ $# == 1 && -d $1 ]] || { echo 'Usage: build-state-module.sh EXISTING_OUTPUT_DIR' >&2; exit 2; }
out=$(cd -- "$1" && pwd -P)
flags=(-std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0 -pthread -Wall -Wextra -Werror -I"$repo/src/include")
libs=(-L"$repo/dest/lib" -Wl,-rpath,"$repo/dest/lib" -lpq_ce -lsse_crypto -lmitra_sse_crypto -lsodium -lgmp)
# Build the checked-out module; never replace an installed library.
"${CXX:-g++}" "${flags[@]}" -fPIC -shared -DSTATE_VERIFY_LIBRARY -Wl,-z,defs -Wl,-soname,libstate_verify.so \
  "$repo/src/common/interfaces/libpq/row_state_v1.cpp" \
  "$repo/src/common/interfaces/libpq/row_state_catalog.cpp" \
  "$here/runtime-check.cpp" "${libs[@]}" -ldl -o "$out/libstate_verify.so"
"${CXX:-g++}" "${flags[@]}" -fPIC -pie "$here/runtime-check.cpp" \
  -L"$out" -Wl,-rpath,"$out" -lstate_verify "${libs[@]}" -ldl -o "$out/runtime-check"
sha256sum "$repo/src/common/interfaces/libpq/row_state_v1.cpp" \
  "$repo/src/common/interfaces/libpq/row_state_catalog.cpp" "$out/libstate_verify.so" > "$out/module-input.sha256"
chmod 0755 "$out/libstate_verify.so" "$out/runtime-check"
"$out/runtime-check" "$out/libstate_verify.so"
