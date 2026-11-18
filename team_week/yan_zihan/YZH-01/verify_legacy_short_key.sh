#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd -- "$1" && pwd)
base=25489f563f25b522d0cd37a6c85865039e3f340f
source_file=src/common/interfaces/libpq/sse_common.cpp
cxx=${CXX:-g++}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
(
  cd "$repo"
  git rev-parse "$base:$source_file"
  git archive "$base" "$source_file" src/include/libpq/sse_common.h src/include/libpq/SSE_map.h | tar -x -C "$tmp"
)
cp "$tmp/$source_file" "$tmp/original.cpp"
(cd "$tmp" && git apply --check "$here/legacy-short-key.patch" && git apply "$here/legacy-short-key.patch")
for variant in baseline fixed; do
  src="$tmp/original.cpp"
  if [[ "$variant" == fixed ]]; then src="$tmp/$source_file"; fi
  "$cxx" -std=c++11 -O0 -ffunction-sections -fdata-sections -I"$tmp/src/include" \
    "$here/tests/legacy_short_key.cpp" "$src" -Wl,--gc-sections -static-libstdc++ \
    -static-libgcc -lcrypto -o "$tmp/check"
  "$tmp/check" "$tmp" "$variant"
done
