#!/usr/bin/env python3
"""Regressions for the delivered entry, independent of a running database."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import shutil

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


if __name__ == '__main__':
    unittest.main(verbosity=2)
