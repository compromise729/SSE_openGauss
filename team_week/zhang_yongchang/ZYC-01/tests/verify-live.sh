#!/usr/bin/env bash
# Run from a new LZM-01 isolated session. This test creates disposable legacy Fides tables.
set -euo pipefail

repo=${OPENGAUSS_PROJECT_ROOT:?Start a new LZM-01 isolated session first}
entry=${ISOLATED_ENTRY:?Missing LZM-01 entry point}
run=${ISOLATED_RUN_DIR:?Missing LZM-01 run directory}
counter="$run/client/fides_client.mat"
tests="$repo/team_week/zhang_yongchang/ZYC-01/tests"
logs=$(mktemp -d "$run/evidence/zyc01.XXXXXXXX")
echo "ZYC01_LIVE_LOG_DIR=$logs"

"$entry" gsql -f "$repo/sse_sql/SSE_init.sql" > "$logs/init.log" 2>&1
"$entry" gsql > "$logs/seed.log" 2>&1 <<'SQL'
fides_setup;
CREATE TABLE zyc01_guard_sample (ind text, keyword text SSE='FIDES', payload text);
INSERT INTO zyc01_guard_sample VALUES ('1','it','short keyword sentinel'),
    ('2','red AND green','literal AND sentinel'),('3','中文 关键词','UTF8 space sentinel'),
    ('4','reinsertfault','reinsert failure sentinel');
CREATE TABLE zyc01_quoted_sample (ind text, keyword text SSE='FIDES', payload text);
INSERT INTO public.zyc01_quoted_sample_data VALUES ('1','it''s OR not','quoted keyword sentinel');
CREATE TABLE zyc01_plain_sample (id integer, payload text);
INSERT INTO zyc01_plain_sample VALUES (1,'ordinary SQL sentinel');
SQL

failures=0
run_case() {
    local name=$1 sql=$2 expected_status=$3 expected_text=$4 expected_counter=$5
    local before after status=0 same=no
    before=$(sha256sum "$counter" | cut -d ' ' -f 1)
    printf 'fides_setup;\n%s\n' "$sql" | timeout --kill-after=2s 10s "$entry" gsql > "$logs/$name.log" 2>&1 || status=$?
    after=$(sha256sum "$counter" | cut -d ' ' -f 1)
    if [[ "$before" == "$after" ]]; then same=yes; fi
    if [[ "$status" == "$expected_status" && "$same" == "$expected_counter" ]] &&
        grep -Fq "$expected_text" "$logs/$name.log"; then
        printf 'PASS %s exit=%s counter_unchanged=%s\n' "$name" "$status" "$same"
    else
        printf 'FAIL %s exit=%s counter_unchanged=%s\n' "$name" "$status" "$same" >&2
        cat "$logs/$name.log" >&2
        failures=$((failures + 1))
    fi
}

run_case ordinary "SELECT id, payload FROM zyc01_plain_sample WHERE id=1;" 0 'ordinary SQL sentinel' yes
run_case single_projection "SELECT payload FROM zyc01_guard_sample WHERE keyword='it';" 3 'column projection is not supported' yes
run_case multi_projection "SELECT keyword, payload FROM zyc01_guard_sample WHERE keyword='it';" 3 'column projection is not supported' yes
run_case compound "SELECT * FROM zyc01_guard_sample WHERE keyword='it' AND ind='1';" 3 'compound condition / LIKE is not supported' yes
run_case and_in_keyword "SELECT * FROM zyc01_guard_sample WHERE keyword='red AND green';" 0 'literal AND sentinel' no
run_case utf8_space "SELECT * FROM zyc01_guard_sample WHERE keyword='中文 关键词';" 0 'UTF8 space sentinel' no

# Only the prefix "it" is registered. Truncating the literal would return the sentinel.
run_case doubled_quote "SELECT * FROM zyc01_guard_sample WHERE keyword='it''s OR not';" 3 'keyword is not registered' yes
run_case doubled_quote_suffix "SELECT * FROM zyc01_guard_sample WHERE keyword='it''s' LIMIT 1;" 3 'extra conditions / clauses are not supported' yes

# Seed a real token without exercising the legacy INSERT literal parser.
"${CXX:?Load the openGauss runtime environment first}" -std=c++14 -D_GLIBCXX_USE_CXX11_ABI=0 \
    -I"${GAUSSHOME:?Missing isolated openGauss installation}/include" -I"$repo/src/include" \
    -I"$repo/sse/opensse-schemes/third_party/crypto/src/include" \
    "$tests/seed-quoted-keyword.cpp" -L"$GAUSSHOME/lib" \
    -lpq_ce -lsse_crypto -lmitra_sse_crypto \
    -o "$run/private/seed-quoted-keyword" > "$logs/seed-build.log" 2>&1
"$run/private/seed-quoted-keyword" "$run/socket" "$ISOLATED_PORT" > "$logs/seed-index.log" 2>&1
run_case quoted_hit "SELECT * FROM zyc01_quoted_sample WHERE keyword='it''s OR not';" 0 'quoted keyword sentinel' no
run_case quoted_hit_after_reinsert "SELECT * FROM zyc01_quoted_sample WHERE keyword='it''s OR not';" 0 'quoted keyword sentinel' no
if ! grep -Fxq "it's OR not 3" "$counter"; then
    echo 'FAIL reinsert did not reuse the full decoded keyword' >&2
    failures=$((failures + 1))
fi

"$entry" gsql -c 'DROP FUNCTION public.fides_re_insert(text[], text[], text[], text);' > "$logs/reinsert-fixture.log" 2>&1
run_case reinsert_failed "SELECT * FROM zyc01_guard_sample WHERE keyword='reinsertfault';" 3 'Fides re-insert failed' no
if grep -Fq 'reinsert failure sentinel' "$logs/reinsert_failed.log"; then
    echo 'FAIL reinsert failure displayed business data' >&2
    failures=$((failures + 1))
fi
printf 'ZYC01_LIVE_FAILURES=%s\n' "$failures"
[[ $failures == 0 ]]