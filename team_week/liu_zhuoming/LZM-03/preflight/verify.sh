#!/usr/bin/env bash
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "${OPENGAUSS_PROJECT_ROOT:-$here/../../../..}" && pwd -P)
out=$(mktemp -d /var/tmp/state-crypto-test.XXXXXXXX)
echo "EVIDENCE_DIR=$out"
chmod 0711 "$out"
# Current compiler/dependencies only. Never build/install into dest or src.
flags=(-std=c++11 -pthread -Wall -Wextra -Werror -I"$repo/src/include"
       -I"$repo/team_week/yan_zihan/YZH-01/include")
if [[ -n ${OPENSSL_INCLUDE_DIR:-} || -n ${OPENSSL_LIB_DIR:-} ]]; then
    flags+=(-I"${OPENSSL_INCLUDE_DIR:?matching headers required}"
            -L"${OPENSSL_LIB_DIR:?matching library required}" -Wl,-rpath,"$OPENSSL_LIB_DIR")
fi
find "$repo/dest/lib" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/installed.before.sha256"
"${CXX:-g++}" "${flags[@]}" "$here/state-crypto-cases.cpp" \
    "$repo/src/common/interfaces/libpq/row_state_v1.cpp" \
    "$repo/team_week/yan_zihan/YZH-01/src/row_aead_v1.cpp" \
    -Wl,--wrap=renameat -lcrypto -o "$out/state-crypto-cases"
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -fPIC -shared \
    "$here/fault-hook.cpp" -ldl -o "$out/fault-hook.so"
chmod 0755 "$out/state-crypto-cases" "$out/fault-hook.so"
if (( EUID == 0 )); then
    install -d -m 0700 -o omm -g dbgrp "$out/private"
    owner=(runuser -u omm --)
else
    mkdir -m 0700 "$out/private"
    owner=(env)
fi
cases=(KEY-01 KEY-02 KEY-03 KEY-04 KEY-05 KEY-06 KEY-07 KEY-08
       FLOW-01 FLOW-02 FLOW-03 FLOW-04 FLOW-05 ORDER-01 ORDER-02 ORDER-03 ORDER-04 HOOK-01)
module_rc=0
for name in "${cases[@]}"; do
    rc=0
    "${owner[@]}" env LD_PRELOAD="$out/fault-hook.so" ISOLATED_RUN_DIR="$out/private" \
        "$out/state-crypto-cases" "$out/private" "$name" > "$out/$name.json" 2> "$out/$name.stderr" || rc=$?
    printf '%s\n' "$rc" > "$out/$name.exit"
    if (( rc != 0 )); then module_rc=1; fi
    echo "CASE=$name EXIT=$rc"
    cat "$out/$name.json" "$out/$name.stderr"
done
audit_rc=0
"${PYTHON:-python3}" "$here/audit-call-order.py" --repo "$repo" --output "$out/call-order.json" \
    > "$out/call-order.log" 2>&1 || audit_rc=$?
cat "$out/call-order.log"
find "$repo/dest/lib" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/installed.after.sha256"
cmp "$out/installed.before.sha256" "$out/installed.after.sha256"
echo 'PASS installed library file list and SHA-256 unchanged' > "$out/environment.log"
sha256sum "$repo/src/common/interfaces/libpq/row_state_v1.cpp" \
    "$repo/team_week/yan_zihan/YZH-01/src/row_aead_v1.cpp" \
    "$repo/src/include/libpq/sse_v1_contract.hpp" "$here/"* > "$out/source-input.sha256"
export TEST_EVIDENCE_DIR="$out" MODULE_EXIT="$module_rc" AUDIT_EXIT="$audit_rc"
"${PYTHON:-python3}" - <<'PY'
import json, os
from pathlib import Path
out = Path(os.environ['TEST_EVIDENCE_DIR'])
cases = []
for path in sorted(out.glob('*.exit')):
    rc = int(path.read_text())
    record = {'case': path.stem, 'exit_code': rc, 'result': 'FAIL'}
    if rc == 0:
        try:
            record.update(json.loads(path.with_suffix('.json').read_text()))
        except (ValueError, OSError):
            record['result'] = 'FAIL'
    cases.append(record)
module_ok = os.environ['MODULE_EXIT'] == '0' and all(r['result'] == 'PASS' for r in cases)
result = {'module_result': 'PASS' if module_ok else 'FAIL', 'cases': cases,
          'case_count': len(cases), 'call_order_exit': int(os.environ['AUDIT_EXIT']),
          'release_result': 'NOT_RELEASED',
          'scope': 'real state and crypto implementations; catalog and network effects are explicit fixtures',
          'private_evidence_dir': str(out / 'private'),
          'full_business_chain': 'not executed; see call-order.json'}
(out / 'results.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print('MODULE_RESULT=' + result['module_result'])
print('RELEASE_RESULT=' + result['release_result'])
if not module_ok:
    raise SystemExit(1)
PY
if (( module_rc != 0 )); then exit 1; fi
# An absent client integration must be visible to batch callers.
exit "$audit_rc"
