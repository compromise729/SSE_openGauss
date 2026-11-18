#!/usr/bin/env bash
set -eo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
build=$(mkdir -p -- "$root/build" && cd -- "$root/build" && pwd)
cxx=${CXX:-g++}
read -r -a extra_cxxflags <<< "${CXXFLAGS:-}"
read -r -a extra_ldflags <<< "${LDFLAGS:-}"
crypto_cflags=()
crypto_libs=(-lcrypto)
if [[ -n "${OPENSSL_INCLUDE_DIR:-}" || -n "${OPENSSL_LIB_DIR:-}" ]]; then
  : "${OPENSSL_INCLUDE_DIR:?set the matching OpenSSL include directory}"
  : "${OPENSSL_LIB_DIR:?set the matching OpenSSL library directory}"
  crypto_cflags=(-I"$OPENSSL_INCLUDE_DIR")
  crypto_libs=(-L"$OPENSSL_LIB_DIR" -lcrypto)
fi
flags=(-std=c++11 -Wall -Wextra -Werror -O2 -g -I"$root/include")

"$cxx" "${flags[@]}" "${extra_cxxflags[@]}" "${crypto_cflags[@]}" -fPIC -shared \
  "$root/src/row_aead_v1.cpp" "${extra_ldflags[@]}" \
  -Wl,-z,defs -Wl,-soname,librow_aead_v1.so "${crypto_libs[@]}" -o "$build/librow_aead_v1.so"
"$cxx" "${flags[@]}" "${extra_cxxflags[@]}" "$root/tests/test_crypto.cpp" \
  "${extra_ldflags[@]}" -Wl,-rpath,'$ORIGIN' -L"$build" -lrow_aead_v1 -o "$build/test_crypto"
"$cxx" -Wall -Wextra -Werror -fPIC -shared "$root/tests/fail_random.cpp" -o "$build/test_fail_random.so"
printf 'built librow_aead_v1.so (%s)\n' "$("$cxx" --version | sed -n '1p')"
