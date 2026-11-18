#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
mkdir -p -- "$root/results"
{
  printf '$ bash build.sh\n'
  bash "$root/build.sh"
  printf '$ ldd build/librow_aead_v1.so\n'
  runtime=$(ldd "$root/build/librow_aead_v1.so")
  printf '%s\n' "$runtime"
  if [[ "$runtime" == *'not found'* ]]; then exit 1; fi
  printf '$ bash run.sh\n'
  bash "$root/run.sh"
} 2>&1 | tee "$root/results/module.log"
