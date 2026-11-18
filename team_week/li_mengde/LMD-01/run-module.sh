#!/usr/bin/env bash
# Invoke in a fresh disposable database with SSE_init.sql already installed.
set -euo pipefail
if [[ $# != 4 ]]; then
    echo 'Usage: bash run-module.sh SOCKET_DIRECTORY PORT DATABASE ISOLATED_CLIENT_DIRECTORY' >&2
    exit 2
fi
here=$(cd -- "$(dirname -- "$0")" && pwd)
schema="$here/SSE_v1.sql"
if [[ ! -f "$schema" ]]; then schema="$here/../../../sse_sql/SSE_v1.sql"; fi
socket=$(realpath -- "$1")
state=$(realpath -- "$4")
repo=/opt/og/openGauss-server
if [[ ! -S "$socket/.s.PGSQL.$2" || ! "$socket" -ef "$state/../socket" ||
      ! -d "$state" || ! -d "$repo/sse_storage" ]]; then
    echo 'Use a local test socket directory and a separate client state directory.' >&2
    exit 2
fi
if [[ ! "$state" -ef "$repo/sse_storage" || "$state" == "$repo/sse_storage" ]]; then
    echo 'Bind the isolated client state in the same private namespace as the server.' >&2
    exit 2
fi
cd "$here"
sql=(gsql -X -h "$socket" -p "$2" -d "$3" -v ON_ERROR_STOP=1)
scratch=$(mktemp -d)
phase=setup
finish() {
    local status=$?
    rm -f -- "$scratch"/{audit,check,before,after,db-before,db-after,row_aead_tables.v1,sse_table.mat}
    rmdir -- "$scratch"
    printf '{"task":"LMD-01","phase":"%s","exit_code":%s,"passed":%s}\n' \
        "$phase" "$status" "$([[ $status == 0 ]] && echo true || echo false)"
}
trap finish EXIT

"${sql[@]}" -c 'SELECT current_database(), version()'
"${sql[@]}" -f "$schema"
"${sql[@]}" -f storage-fixture.sql

phase=create_registration
fixture_create() {
    local logical_name=$1
    "${sql[@]}" -v logical_name="$logical_name" -f - <<'SQL' || return $?
SELECT public.sse_v1_create('public', :'logical_name');
SQL
    printf '%s\t%s\tpublic\t%s\t1\n' "$deployment" "$database_oid" "$logical_name" >> "$scratch/row_aead_tables.v1" || return
    printf '%s keyword|fides\n' "$logical_name" >> "$scratch/sse_table.mat"
}

mapping_digest() {
    sha256sum "$scratch/row_aead_tables.v1" "$scratch/sse_table.mat"
}

deployment=11111111111111111111111111111111
database_oid=$("${sql[@]}" -Atq -c "SELECT oid FROM pg_database WHERE datname=current_database()" |
    sed -n '/^[0-9][0-9]*$/p')
[[ "$database_oid" =~ ^[0-9]+$ ]]
printf 'SSE_ROW_TABLES_V1\n' > "$scratch/row_aead_tables.v1"
printf 'lmd_legacy keyword|fides\n' > "$scratch/sse_table.mat"
fixture_create lmd_registration_ok
grep -F $'\tpublic\tlmd_registration_ok\t1' "$scratch/row_aead_tables.v1"
grep -Fx 'lmd_registration_ok keyword|fides' "$scratch/sse_table.mat"
"${sql[@]}" -c "SELECT public.sse_v1_require_table('public','lmd_registration_ok')"
echo 'PASS test caller registered both mappings after successful server create'

# Force catalog insertion to fail after both physical CREATE TABLE statements.
"${sql[@]}" -c "ALTER TABLE public.sse_v1_catalog ADD CONSTRAINT lmd_fixture_reject
    CHECK (logical_table <> 'lmd_registration_fail')"
mapping_digest > "$scratch/before"
if fixture_create lmd_registration_fail > "$scratch/check" 2>&1; then
    echo 'FAIL test caller accepted failed server create' >&2
    exit 1
else
    printf 'PASS test caller received nonzero create status=%s\n' "$?"
fi
cat "$scratch/check"
grep -F 'violates check constraint "lmd_fixture_reject"' "$scratch/check" >/dev/null
mapping_digest > "$scratch/after"
cat "$scratch/before" "$scratch/after"
diff -u "$scratch/before" "$scratch/after"
"${sql[@]}" -f - <<'SQL'
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_tables WHERE schemaname = 'public'
               AND tablename IN ('lmd_registration_fail_data','lmd_registration_fail_index'))
       OR EXISTS (SELECT 1 FROM public.sse_v1_catalog WHERE logical_table = 'lmd_registration_fail') THEN
        RAISE EXCEPTION 'failed create left partial database state';
    END IF;
    RAISE NOTICE 'PASS catalog rejection rolled back both physical tables and left no catalog entry';
END;
$$;
BEGIN;
ALTER TABLE public.sse_v1_catalog DROP CONSTRAINT lmd_fixture_reject;
DROP TABLE public.lmd_registration_ok_data, public.lmd_registration_ok_index;
DELETE FROM public.sse_v1_catalog WHERE logical_schema = 'public' AND logical_table = 'lmd_registration_ok';
COMMIT;
SQL
echo 'PASS failed create left both fixture mapping files byte-identical'

phase=read_only_audit
check_output() {
    local status=0
    "$@" > "$scratch/audit" 2>&1 || status=$?
    cat "$scratch/audit"
    if (( status != 0 )); then return "$status"; fi
    if grep -Eq '(NOTICE|WARNING|ERROR):[[:space:]]+(FAIL|SKIP)([[:space:]:]|$)' "$scratch/audit"; then
        return 4
    fi
}

audit() {
    check_output "${sql[@]}" -v target_schema="$1" -v target="$2" -v profile="$3" -f 李孟德-check.sql
}

snapshot() {
    "${sql[@]}" -Atq <<'SQL' | sed '/^total time: /d'
SELECT count(*), md5(string_agg(rid || ':' || md5(data_blob), ',' ORDER BY rid)) FROM public.lmd_sample_data;
SELECT count(*), md5(string_agg(ut || ':' || md5(locator_blob), ',' ORDER BY ut)) FROM public.lmd_sample_index;
SQL
    find "$state" -maxdepth 1 -type f \( -name '*.mat' -o -name '*.key' \) -print0 |
        sort -z | xargs -0 -r sha256sum
}

snapshot > "$scratch/db-before"
audit public lmd_sample fides_v1
audit public lmd_sample fides_v1
snapshot > "$scratch/db-after"
diff -u "$scratch/db-before" "$scratch/db-after"
echo 'PASS audit preserved data, index and local counter/key file digests'
audit public lmd_legacy fides

expect_failure() {
    local status
    if audit "$1" "$2" "$3" > "$scratch/check" 2>&1; then
        cat "$scratch/check"
        echo 'FAIL a required check exited zero' >&2
        exit 1
    else
        status=$?
    fi
    cat "$scratch/check"
    grep -F "$4" "$scratch/check" >/dev/null
    printf 'PASS audit rejected %s (%s) with exit code %s\n' "$2" "$3" "$status"
}

expect_failure public missing fides_v1 'FAIL required schema checks failed'
expect_failure public lmd_legacy fides_v1 'FAIL required schema checks failed'
if "${sql[@]}" -c 'fides_v1_select * from public.lmd_legacy_index where token= and derivation_key= and add_count=0;' \
    > "$scratch/check" 2>&1; then
    echo 'FAIL v1 wire entry accepted an old table' >&2
    exit 1
fi
grep -F 'unregistered sse_v1 table: lmd_legacy' "$scratch/check"
echo 'PASS v1 wire entry rejected the old Fides table'
"${sql[@]}" -c "INSERT INTO public.lmd_legacy_data VALUES ('1','fixture','duplicate')"
expect_failure public lmd_legacy fides 'FAIL duplicate IDs found'
for verdict in FAIL SKIP; do
    if check_output "${sql[@]}" -c "DO \$\$ BEGIN RAISE NOTICE '$verdict synthetic audit outcome'; END; \$\$"; then
        echo "FAIL summary accepted a $verdict notice" >&2
        exit 1
    else
        [[ $? == 4 ]]
    fi
    printf 'PASS summary rejected a %s notice despite successful gsql execution\n' "$verdict"
done
echo 'PASS module fixture, positive and negative read-only audits'
phase=complete
