#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 5 ]]; then
    echo 'Usage: bash run-roundtrip.sh SOCKET_DIRECTORY PORT DATABASE ISOLATED_CLIENT_DIRECTORY YZH01_DIRECTORY' >&2
    exit 2
fi
here=$(cd -- "$(dirname -- "$0")" && pwd)
schema="$here/../LMD-01/SSE_v1.sql"
if [[ ! -f "$schema" ]]; then schema="$here/../../../sse_sql/SSE_v1.sql"; fi
socket=$(realpath -- "$1")
state=$(realpath -- "$4")
crypto_source=$(realpath -- "$5")
repo=/opt/og/openGauss-server
if [[ ! -S "$socket/.s.PGSQL.$2" || ! "$socket" -ef "$state/../socket" ||
      ! "$state" -ef "$repo/sse_storage" || "$state" == "$repo/sse_storage" ]]; then
    echo 'Use a disposable database and the matching isolated Fides client state.' >&2
    exit 2
fi
: "${CXX:?Load the openGauss runtime environment first}"
: "${GAUSSHOME:?Load the openGauss runtime environment first}"
: "${BINARYLIBS:?Load the openGauss runtime environment first}"
scratch=$(mktemp -d /tmp/lmd-roundtrip.XXXXXXXX)
trap 'rm -rf -- "$scratch"' EXIT
mkdir -m 700 "$scratch/keys" "$scratch/yzh"
cp -R -- "$crypto_source"/{build.sh,run.sh,include,src,tests} "$scratch/yzh/"

export OPENSSL_INCLUDE_DIR="$BINARYLIBS/dependency/centos7.6_x86_64/openssl/comm/include"
export OPENSSL_LIB_DIR="$GAUSSHOME/lib"
export CXXFLAGS=-D_GLIBCXX_USE_CXX11_ABI=0
bash "$scratch/yzh/build.sh"
bash "$scratch/yzh/run.sh"

sse="$repo/sse/opensse-schemes"
crypto="$sse/third_party/crypto"
"$CXX" -std=c++11 "$CXXFLAGS" -O2 -Wall -Wextra \
    -I"$GAUSSHOME/include" -I"$repo/src/include" -I"$sse/lib/include" \
    -I"$crypto/src/include" -I"$scratch/yzh/include" \
    "$here/fides-roundtrip-probe.cpp" -L"$GAUSSHOME/lib" -L"$crypto/build/src" \
    -L"$scratch/yzh/build" -Wl,-rpath,"$scratch/yzh/build" \
    -lpq_ce -lrow_aead_v1 -lsse_crypto -lmitra_sse_crypto -o "$scratch/fides-roundtrip-probe"

gsql -X -h "$socket" -p "$2" -d "$3" -v ON_ERROR_STOP=1 -f "$schema"
"$scratch/fides-roundtrip-probe" "$socket" "$2" "$3" "$scratch/keys"
gsql -X -h "$socket" -p "$2" -d "$3" -v ON_ERROR_STOP=1 -f "$here/roundtrip-fixture.sql"
