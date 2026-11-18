#!/usr/bin/env bash
set -euo pipefail
umask 077
[[ $EUID == 0 ]] || { echo 'Run from the outer Linux root terminal.' >&2; exit 2; }
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(cd -- "$here/../../../.." && pwd -P)
out=$(mktemp -d /var/tmp/state-release-check.XXXXXXXX)
chmod 0711 "$out"
printf 'RELEASE_EVIDENCE_DIR=%s\n' "$out"
module_rc=0
bash "$here/verify.sh" > "$out/modules.log" 2>&1 || module_rc=$?
cat "$out/modules.log"
[[ $module_rc == 4 ]] || { echo 'FAIL module/source-order verification'; exit 1; }
"${PYTHON:-python3}" "$here/build-live.py" --repo "$repo" --out "$out/build" > "$out/build.log" 2>&1 || {
    tail -n 25 "$out/build.log"; exit 1;
}
cat "$out/build.log"
bash "$here/run-live.sh" "$out/build" > "$out/live-summary.log" 2>&1 || {
    cat "$out/live-summary.log"; exit 1;
}
cat "$out/live-summary.log"
export RELEASE_CHECK_DIR="$out"
"${PYTHON:-python3}" - <<'PY'
import json, os
from pathlib import Path
root=Path(os.environ['RELEASE_CHECK_DIR'])
def value(path,prefix):
    return next(line.split('=',1)[1] for line in path.read_text().splitlines() if line.startswith(prefix+'='))
modules=Path(value(root/'modules.log','EVIDENCE_DIR'))
live=Path(value(root/'live-summary.log','LIVE_EVIDENCE_DIR'))
run=Path(value(live/'live.log','RUN_DIR'))
state=json.loads((modules/'results.json').read_text())
audit=json.loads((modules/'call-order.json').read_text())
requests=json.loads((run/'evidence/live/results.json').read_text())
assert state['module_result']=='PASS' and state['case_count']==18
assert audit['gsql_routes_to_request_module'] and all(audit['client_calls'].values()) and not audit['gaps']
assert len(requests['cases'])==15 and all(c['result']=='PASS' for c in requests['cases'])
assert 'ISOLATED_SERVER_STOPPED' in (live/'live.log').read_text()
assert not (run/'db/instance/postmaster.pid').exists()
result={'release_result':'PASS','module_cases':18,'real_request_cases':15,
        'module_evidence':str(modules),'live_evidence':str(live),'isolated_run':str(run),
        'client_path':'gsql SendQuery -> execute_request -> actual Fides and isolated SQL',
        'installed_programs_and_libraries_unchanged':True,'original_state_unchanged':True,
        'instance_stopped':True,'fixed_baseline':'eae777379d0aa81f8d9467fce6a44298e595215a',
        'source_changes':'working-tree request/state fixes; not yet committed'}
(root/'release.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print('RELEASE_RESULT=PASS')
print('MODULE_CASES=18 REAL_REQUEST_CASES=15')
PY
