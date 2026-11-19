#!/usr/bin/env python3
"""Validate final public evidence and bind it to the checkout and binaries."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

repo, out = [Path(p).resolve() for p in sys.argv[1:]]
baseline = json.loads((out/'baseline-release.json').read_text(encoding='utf-8'))
results = json.loads((out/'requests/results.json').read_text(encoding='utf-8'))
phases = json.loads((out/'requests/phases.json').read_text(encoding='utf-8'))
assert baseline['release_result'] == 'PASS' and baseline['module_cases'] == 18 and baseline['real_request_cases'] == 18
assert len(results['cases']) == 13 and all(c['result'] == 'PASS' for c in results['cases'])
assert len(phases) >= 8
assert baseline['instance_stopped'] and not (Path(baseline['isolated_run'])/'db/instance/postmaster.pid').exists()
assert 'ISOLATED_SERVER_STOPPED' in (out/'zyc-live.log').read_text(encoding='utf-8')
hashes = {}
paths = [repo/p for p in ('dest/bin/gsql', 'dest/bin/gaussdb', 'dest/lib/libpq_ce.so.5.5', 'sse_sql/SSE_v1.sql')]
paths += sorted(p for p in (repo/'team_week/zhang_yongchang/tools').iterdir() if p.is_file())
for p in paths:
    digest = hashlib.sha256()
    with p.open('rb') as f:
        for data in iter(lambda: f.read(1024*1024), b''):
            digest.update(data)
    hashes[str(p.relative_to(repo))] = digest.hexdigest()
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=str(repo)).decode().strip()
dirty = subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no', '--',
    'src/bin/psql/common.cpp', 'src/common/interfaces/libpq', 'sse_sql/SSE_v1.sql',
    'team_week/zhang_yongchang/tools'], cwd=str(repo)).decode()
summary = {'result': 'PASS', 'tested_head': head, 'modified_tracked_inputs': bool(dirty),
           'module_cases': 18, 'baseline_live_cases': 18, 'zyc_cases': 13, 'entry_regressions': 3,
           'successful_search_traces': len(phases), 'instances_stopped': True,
           'public_synthetic_fixtures_only': True, 'derivation_key_values': 'redacted', 'input_sha256': hashes}
(out/'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print('EVIDENCE_VALIDATED=PASS')
