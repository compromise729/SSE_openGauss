#!/usr/bin/env bash
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "${OPENGAUSS_PROJECT_ROOT:-$here/../../../..}" && pwd -P)
[[ $EUID == 0 ]] || { echo 'Run in the outer root shell.' >&2; exit 2; }
out=$(mktemp -d /tmp/lzm02-prerequisites.XXXXXXXX)
echo "EVIDENCE_DIR=$out"
for missing in executable library permission; do
    fake="$out/$missing"
    mkdir -p "$fake/src/bin/psql" "$fake/src/common/interfaces/libpq"
    if [[ $missing == library ]]; then
        ln -s "$repo/src/bin/psql/gsql" "$fake/src/bin/psql/gsql"
    fi
    if [[ $missing == permission ]]; then
        printf 'fixture\n' > "$fake/src/bin/psql/gsql"
        chmod 0600 "$fake/src/bin/psql/gsql"
        ln -s "$repo/src/common/interfaces/libpq/libpq_ce.so.5" "$fake/src/common/interfaces/libpq/libpq_ce.so.5"
    fi
    rc=0
    OPENGAUSS_PROJECT_ROOT="$fake" bash "$here/verify-catalog.sh" > "$out/$missing.log" 2>&1 || rc=$?
    [[ $rc == 1 ]]
    grep -q '^FAIL CATALOG prerequisites:' "$out/$missing.log"
    if grep -q '^EVIDENCE_DIR=' "$out/$missing.log"; then exit 1; fi
done
echo 'PASS CATALOG-08: missing gsql, missing libpq and nonexecutable gsql all fail before instance creation'
