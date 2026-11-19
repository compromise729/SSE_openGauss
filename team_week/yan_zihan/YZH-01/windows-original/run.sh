#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
if [[ ! -x "$root/build/test_crypto" ]]; then
  printf '%s\n' 'Run bash build.sh first.' >&2
  exit 2
fi
scratch=$(mktemp -d /tmp/yzh01.XXXXXXXX)
trap 'rm -rf -- "$scratch"' EXIT
"$root/build/test_crypto" "$scratch" "$root/tests/public-vectors.tsv"
cp "$root/build/test_fail_random.so" "$scratch/rng_fault.so"
LD_PRELOAD="$scratch/rng_fault.so" \
  "$root/build/test_crypto" "$scratch" "$root/tests/public-vectors.tsv" --rng-failure
code=0
"$root/build/test_crypto" "$scratch" "$root/tests/public-vectors.tsv" --last-row \
  >"$scratch/batch.stdout" 2>"$scratch/batch.stderr" || code=$?
if [[ "$code" != 17 || -s "$scratch/batch.stdout" ]] || \
   ! grep -qx 'auth_failed' "$scratch/batch.stderr"; then
  cat "$scratch/batch.stderr" >&2
  printf 'FAIL standalone batch fixture: exit=%s stdout_bytes=%s\n' "$code" \
    "$(wc -c <"$scratch/batch.stdout")" >&2
  exit 1
fi
printf 'PASS standalone batch fixture: last tag damaged, exit=%s stdout_bytes=0 stderr=auth_failed\n' "$code"
