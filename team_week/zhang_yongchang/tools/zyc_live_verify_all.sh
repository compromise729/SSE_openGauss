#!/bin/bash
# Runs INSIDE the isolated omm session (via isolated.sh init)
B=/home/yonghu/shy03_build
LOG=$ISOLATED_RUN_DIR/evidence/zyc-live-evidence.log
: > "$LOG"
log(){ echo "$@" | tee -a "$LOG"; }
log "=== ZYC-02/03 live verify $(date) ==="
log "user=$(id -un) RUN_DIR=$ISOLATED_RUN_DIR PORT=$ISOLATED_PORT"

# 1. Fides base keys + v1 SQL functions
isolated.sh gsql -c "fides_setup;" >>"$LOG" 2>&1 && log "STEP1 fides_setup OK" || log "STEP1 fides_setup FAIL"
chmod 600 "$ISOLATED_RUN_DIR/client/"*.key 2>/dev/null
isolated.sh gsql -f "$OPENGAUSS_PROJECT_ROOT/sse_sql/SSE_v1.sql" >>"$LOG" 2>&1 && log "STEP2 SSE_v1.sql OK" || log "STEP2 SSE_v1.sql FAIL"

# 2. Init v1 state (AEAD keys)
conn="host=$ISOLATED_RUN_DIR/socket port=$ISOLATED_PORT dbname=postgres user=omm"
state="$ISOLATED_RUN_DIR/evidence/v1-state"
"$B/dest/bin/initialize-live" "$conn" "$state" >>"$LOG" 2>&1 && log "STEP3 initialize-live OK state=$state" || log "STEP3 initialize-live FAIL"

# 3. v1 helper: native gsql + SSE_V1_STATE_DIR
vsql(){
  SSE_V1_STATE_DIR="$state" "$B/dest/bin/gsql" -X -h "$ISOLATED_RUN_DIR/socket" -p "$ISOLATED_PORT" \
    -d postgres -U omm -v ON_ERROR_STOP=1 "$@"
}
log "--- STEP4 CREATE ---"
vsql -c "CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text) WITH (sse_storage='ROW_AEAD_V1');" >>"$LOG" 2>&1; log "CREATE rc=$?"
log "--- STEP5 INSERT 3 rows ---"
vsql -c "INSERT INTO week_demo VALUES ('1','apple','red apple'),('2','apple','green apple'),('3','banana','yellow banana');" >>"$LOG" 2>&1; log "INSERT rc=$?"
log "--- STEP6 physical tables (should be ciphertext only) ---"
isolated.sh gsql -c "SELECT rid,data_blob FROM public.week_demo_data;" >>"$LOG" 2>&1
isolated.sh gsql -c "SELECT ut,locator_blob FROM public.week_demo_index;" >>"$LOG" 2>&1
log "--- STEP7 SELECT apple ---"
vsql -c "SELECT * FROM week_demo WHERE keyword='apple';" >>"$LOG" 2>&1; log "SELECT apple rc=$?"
log "--- STEP8 SELECT banana ---"
vsql -c "SELECT * FROM week_demo WHERE keyword='banana';" >>"$LOG" 2>&1; log "SELECT banana rc=$?"
log "--- STEP9 SELECT unknown ---"
vsql -c "SELECT * FROM week_demo WHERE keyword='zyc_unknown_20261119';" >>"$LOG" 2>&1; log "SELECT unknown rc=$?"
log "--- STEP10 repeat SELECT apple (dedup check) ---"
vsql -c "SELECT * FROM week_demo WHERE keyword='apple';" >>"$LOG" 2>&1; log "SELECT apple repeat rc=$?"

log "=== ROUND2 (same session) ==="
log "--- A. apple still intact (resume) ---"
vsql -c "SELECT * FROM week_demo WHERE keyword='apple';" >>"$LOG" 2>&1; log "apple rc=$?"

log "--- B. Chinese + space keyword INSERT ---"
vsql -c "INSERT INTO week_demo VALUES ('4','中文 苹果 space','red 苹果'),('5','中文 苹果 space','green 苹果');" >>"$LOG" 2>&1; log "insert-cn rc=$?"
log "--- C. Chinese + space SELECT ---"
vsql -c "SELECT * FROM week_demo WHERE keyword='中文 苹果 space';" >>"$LOG" 2>&1; log "select-cn rc=$?"

log "--- D. SQL quote escape ('it''s OR not') ---"
vsql -c "INSERT INTO week_demo VALUES ('6','it''s OR not','o''brien apple');" >>"$LOG" 2>&1; log "insert-esc rc=$?"
vsql -c "SELECT * FROM week_demo WHERE keyword='it''s OR not';" >>"$LOG" 2>&1; log "select-esc rc=$?"

log "--- E. unsupported syntax: AND compound (should REJECT) ---"
vsql -c "SELECT * FROM week_demo WHERE keyword='apple' AND payload='red apple';" >>"$LOG" 2>&1; log "select-AND rc=$? (expect NONZERO)"
log "--- F. unsupported: column projection (should REJECT) ---"
vsql -c "SELECT ind FROM week_demo WHERE keyword='apple';" >>"$LOG" 2>&1; log "select-proj rc=$? (expect NONZERO)"
log "--- G. unknown keyword zero rows ---"
vsql -c "SELECT * FROM week_demo WHERE keyword='not_registered_zyc';" >>"$LOG" 2>&1; log "select-unknown rc=$?"

log "--- H. physical tables still ciphertext only (no plaintext) ---"
isolated.sh gsql -c "SELECT rid,data_blob FROM public.week_demo_data;" >>"$LOG" 2>&1
isolated.sh gsql -At -c "SELECT string_agg(data_blob,'|') FROM public.week_demo_data;" 2>/dev/null | grep -qiE "apple|banana|red|green|yellow|中文|苹果" && log "PLAINTEXT LEAK FOUND IN DATA" || log "NO_PLAINTEXT_IN_DATA_OK"
isolated.sh gsql -At -c "SELECT string_agg(locator_blob,'|') FROM public.week_demo_index;" 2>/dev/null | grep -qiE "apple|banana|red|green|yellow|中文|苹果|[0-9a-f]{32}" && log "PLAINTEXT/RAW-RID LEAK IN INDEX" || log "NO_PLAINTEXT_IN_INDEX_OK"

log "=== END $(date) ==="
