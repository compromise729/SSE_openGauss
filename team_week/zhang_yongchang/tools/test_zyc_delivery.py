#!/usr/bin/env python3
"""Regressions for the delivered entry, independent of a running database."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import shutil
import ast
import json
import base64
import sys

HERE = Path(__file__).resolve().parent
ENTRY = HERE / 'zyc_live_verify_all.sh'


class DeliveryEntryTest(unittest.TestCase):
    def test_checkout_script_is_valid_bash(self):
        p = subprocess.run(['bash', '-n', str(ENTRY)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(p.returncode, 0, p.stderr.decode(errors='replace'))

    def test_missing_install_and_failed_sql_never_report_pass(self):
        with tempfile.TemporaryDirectory(prefix='zyc entry with spaces ') as name:
            root = Path(name)
            (root/'bin').mkdir()
            stub = root/'bin/isolated.sh'
            stub.write_text('#!/bin/bash\necho injected_SQL_failure >&2\nexit 23\n', encoding='utf-8')
            stub.chmod(0o700)
            (root/'run/evidence').mkdir(parents=True)
            (root/'run/client').mkdir()
            # Normalize only the temporary copy to test failure propagation
            # independently of the separate, original-file syntax regression.
            candidate = root/'entry.sh'
            candidate.write_bytes(ENTRY.read_bytes().replace(b'\r\n', b'\n'))
            env = dict(os.environ, PATH=str(root/'bin')+':'+os.environ['PATH'],
                       ISOLATED_RUN_DIR=str(root/'run'), ISOLATED_PORT='59999',
                       OPENGAUSS_PROJECT_ROOT=str(root/'absent installation'))
            p = subprocess.run(['bash', str(candidate), '--project-root', env['OPENGAUSS_PROJECT_ROOT'],
                                '--evidence-dir', str(root/'public evidence')], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(p.returncode, 0, 'failed/missing database commands returned success')
            output = p.stdout.decode(errors='replace')
            self.assertNotIn('NO_PLAINTEXT_IN_DATA_OK', output)
            self.assertNotIn('NO_PLAINTEXT_IN_INDEX_OK', output)
            self.assertNotIn('ZYC_RESULT=PASS', output)
            # Exercise missing-install validation separately from nested-session rejection.
            env.pop('ISOLATED_RUN_DIR')
            missing = subprocess.run(['bash', str(ENTRY), '--project-root', env['OPENGAUSS_PROJECT_ROOT']],
                                     env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn(b'project root does not exist', missing.stderr)

    @unittest.skipUnless(os.geteuid() == 0, 'outer root entry requires Linux root')
    def test_outer_entry_propagates_failed_validation_stage(self):
        # Only the expensive external validation boundary is replaced; the
        # delivered entry, option parsing, process execution and ERR path run.
        with tempfile.TemporaryDirectory(prefix='zyc-bootstrap-') as name:
            root = Path(name)
            files = ['dest/bin/gsql', 'dest/bin/gaussdb', 'dest/lib/libpq_ce.so.5.5',
                     'sse_sql/SSE_v1.sql', 'team_week/liu_zhuoming/LZM-03/preflight/initialize-live.cpp',
                     'team_week/liu_zhuoming/LZM-01/isolated.sh',
                     'team_week/zhang_yongchang/tools/zyc_session.sh',
                     'team_week/zhang_yongchang/tools/test_zyc_delivery.py']
            for relative in files:
                path = root/relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('', encoding='utf-8')
            stage = root/'team_week/liu_zhuoming/LZM-03/preflight/verify-all.sh'
            stage.write_text('#!/bin/bash\necho injected_validation_failure\nexit 23\n', encoding='utf-8')
            env = dict(os.environ, CXX=shutil.which('g++'))
            env.pop('ISOLATED_RUN_DIR', None)
            p = subprocess.run(['bash', str(ENTRY), '--project-root', str(root),
                                '--evidence-dir', str(root/'report')], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(p.returncode, 23, p.stderr.decode(errors='replace'))
            self.assertNotIn(b'ZYC_RESULT=PASS', p.stdout)
            self.assertFalse((root/'report/summary.json').exists())
            self.assertIn('injected_validation_failure', (root/'report/baseline.log').read_text())

    def test_one_missing_successful_search_trace_is_rejected(self):
        # Exercise the real phase validator, isolating only AEAD decoding,
        # which is separately checked against real ciphertext by the live run.
        source = HERE/'zyc_cases.py'
        node = next(n for n in ast.parse(source.read_text(encoding='utf-8')).body
                    if isinstance(n, ast.FunctionDef) and n.name == 'verify_wire_phases')
        with tempfile.TemporaryDirectory(prefix='zyc-missing-trace-') as name:
            root = Path(name)
            rid = 'a'*32
            events = ['search_take_sent','reinsert_sent','fetch_sent','rows_authenticated','search_commit','complete_durable']
            requests = [{'request': i, 'exit': 0, 'events': events, 'expected_kind': 'search'} for i in range(1,11)]
            calls = [{'sql': 'fides_v1_select * from public.week_demo_index where token=QQ== and derivation_key=<redacted> and add_count=1;',
                      'params': [], 'status': 'PGRES_TUPLES_OK', 'rows': [['QQ==','fixture']]},
                     {'sql': 'SELECT public.sse_v1_reinsert(', 'params': ['week_demo','{"QQ=="}','{"fixture"}'],
                      'status': 'PGRES_TUPLES_OK', 'rows': []},
                     {'sql': 'SELECT public.sse_v1_fetch(', 'params': ['week_demo','{"'+rid+'"}'],
                      'status': 'PGRES_TUPLES_OK', 'rows': [[rid,'fixture']]}]
            for i in range(1,10):
                (root/('request-%02d.wire.jsonl'%i)).write_text(''.join(json.dumps(c)+'\n' for c in calls), encoding='utf-8')
            def require(ok, message):
                if not ok:
                    raise RuntimeError(message)
            scope = dict(issued_requests=requests, output=root, json=json, base64=base64, require=require,
                         independently_decode=lambda kind, values: [{'rid':rid,'operation':0}])
            try:
                from zyc_checks import validate_request_coverage
                scope['validate_request_coverage'] = validate_request_coverage
            except ImportError:
                pass
            exec(compile(ast.Module(body=[node]), str(source), 'exec'), scope)
            for missing_kind in ('search','insert','create'):
                requests[-1]['expected_kind'] = missing_kind
                with self.subTest(missing_kind=missing_kind):
                    with self.assertRaises((RuntimeError, ValueError)):
                        scope['verify_wire_phases']()

    def test_optimized_python_cannot_approve_invalid_evidence(self):
        with tempfile.TemporaryDirectory(prefix='zyc-optimized-') as name:
            root = Path(name); repo = root/'repo'; report = root/'report'
            (repo/'team_week/zhang_yongchang/tools').mkdir(parents=True)
            for name in ('dest/bin/gsql','dest/bin/gaussdb','dest/lib/libpq_ce.so.5.5','sse_sql/SSE_v1.sql'):
                p=repo/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(b'fixture')
            subprocess.run(['git','init','-q',str(repo)], check=True)
            subprocess.run(['git','-c','user.name=Fixture','-c','user.email=fixture@example.invalid',
                            'commit','--allow-empty','-qm','fixture'], cwd=str(repo), check=True)
            (report/'requests').mkdir(parents=True)
            (report/'baseline-release.json').write_text(json.dumps({'release_result':'FAIL','module_cases':0,
                'real_request_cases':0,'instance_stopped':False,'isolated_run':str(root/'not-stopped')}))
            (report/'requests/results.json').write_text('{"cases":[]}')
            (report/'requests/phases.json').write_text('[]')
            (report/'zyc-live.log').write_text('deliberate failure\n')
            p = subprocess.run([sys.executable,'-O',str(HERE/'zyc_evidence.py'),str(repo),str(report)],
                               env=dict(os.environ,PYTHONOPTIMIZE='1'), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(p.returncode, 0, 'optimized Python approved invalid evidence')
            self.assertFalse((report/'summary.json').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
