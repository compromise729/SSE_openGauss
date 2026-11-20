#!/usr/bin/env python3
"""Validate final public evidence and bind it to the checkout and binaries."""
import hashlib
import json
import os
from pathlib import Path
import pwd
import subprocess
import sys
from zyc_checks import validate_request_coverage

def require(ok, message):
    if not ok:
        raise ValueError(message)

repo, out = [Path(p).resolve() for p in sys.argv[1:]]
baseline = json.loads((out/'baseline-release.json').read_text(encoding='utf-8'))
results = json.loads((out/'requests/results.json').read_text(encoding='utf-8'))
phases = json.loads((out/'requests/phases.json').read_text(encoding='utf-8'))
require(baseline['release_result'] == 'PASS' and baseline['module_cases'] == 18 and baseline['real_request_cases'] == 18, 'baseline did not pass')
require(len(results['cases']) == 13 and all(c['result'] == 'PASS' for c in results['cases']), 'ZYC cases did not pass')
requests = json.loads((out/'requests/requests.json').read_text(encoding='utf-8'))
validate_request_coverage(requests, out/'requests')
expected = {r['request'] for r in requests if r['exit'] == 0 and r['expected_kind'] == 'search'}
require(expected and len(phases) == len(expected) and {p['request'] for p in phases} == expected, 'search phase coverage incomplete')
require(baseline['instance_stopped'] and not (Path(baseline['isolated_run'])/'db/instance/postmaster.pid').exists(), 'baseline instance not stopped')
require('ISOLATED_SERVER_STOPPED' in (out/'zyc-live.log').read_text(encoding='utf-8'), 'ZYC instance not stopped')
hashes = {}
paths = [repo/p for p in ('dest/bin/gsql', 'dest/bin/gaussdb', 'dest/lib/libpq_ce.so.5.5', 'sse_sql/SSE_v1.sql')]
paths += sorted(p for p in (repo/'team_week/zhang_yongchang/tools').iterdir() if p.is_file())
for p in paths:
    digest = hashlib.sha256()
    with p.open('rb') as f:
        for data in iter(lambda: f.read(1024*1024), b''):
            digest.update(data)
    hashes[str(p.relative_to(repo))] = digest.hexdigest()
git = ['git']
checkout_owner = repo.stat().st_uid
if os.geteuid() == 0 and checkout_owner != 0:
    git = ['runuser', '-u', pwd.getpwuid(checkout_owner).pw_name, '--', 'git']
head = subprocess.check_output(git + ['rev-parse', 'HEAD'], cwd=str(repo)).decode().strip()
# Git 1.8 may rewrite the index even for a status check. Inspect patches
# without refreshing it, and distinguish stat-only changes from changed bytes.
diff = git + ['-c', 'diff.autoRefreshIndex=false', 'diff', '--no-ext-diff', '--no-textconv']
tracked_inputs = ['--', 'src/bin/psql/common.cpp', 'src/common/interfaces/libpq',
                  'sse_sql/SSE_v1.sql', 'team_week/zhang_yongchang/tools']
dirty = any(subprocess.check_output(diff + scope + tracked_inputs, cwd=str(repo))
            for scope in (['--cached', 'HEAD'], []))
summary = {'result': 'PASS', 'tested_head': head, 'modified_tracked_inputs': dirty,
           'python_optimized': bool(sys.flags.optimize),
           'module_cases': 18, 'baseline_live_cases': 18, 'zyc_cases': 13, 'entry_regressions': 5,
           'successful_search_traces': len(phases), 'instances_stopped': True,
           'public_synthetic_fixtures_only': True, 'derivation_key_values': 'redacted', 'input_sha256': hashes}
(out/'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print('EVIDENCE_VALIDATED=PASS')
