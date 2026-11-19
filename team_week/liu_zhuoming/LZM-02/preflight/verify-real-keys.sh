#!/usr/bin/env bash
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "${OPENGAUSS_PROJECT_ROOT:-$here/../../../..}" && pwd -P)
yzh="$repo/team_week/yan_zihan/YZH-01"
out=$(mktemp -d /tmp/lzm02-real-keys.XXXXXXXX)
trap 'rm -rf -- "$out"' EXIT
openssl_flags=(-pthread)
if [[ -n ${OPENSSL_INCLUDE_DIR:-} || -n ${OPENSSL_LIB_DIR:-} ]]; then
    openssl_flags+=(-I"${OPENSSL_INCLUDE_DIR:?Set the matching OpenSSL headers}"
                   -L"${OPENSSL_LIB_DIR:?Set the matching OpenSSL library}"
                   -Wl,-rpath,"$OPENSSL_LIB_DIR")
fi
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -I"$repo/src/include" -I"$yzh/include" \
    "${openssl_flags[@]}" "$here/real-key-init.cpp" "$repo/src/common/interfaces/libpq/row_state_v1.cpp" \
    "$yzh/src/row_aead_v1.cpp" -lcrypto -o "$out/real-key-init"
"$out/real-key-init" "$out"
