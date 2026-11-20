#!/usr/bin/env python3
"""Check a fresh native installation using public SQL fixtures."""
import base64
from collections import Counter
import json
import os
from pathlib import Path
import re
import subprocess
import sys


run = Path(os.environ['ISOLATED_RUN_DIR'])
repo = Path(os.environ['OPENGAUSS_PROJECT_ROOT'])
state = run / 'evidence/shy05-state'
gsql = repo / 'dest/bin/gsql'
prefix = [str(gsql), '-X', '-q', '-A', '-t', '-F', '|',
          '-h', str(run/'socket'), '-p', os.environ['ISOLATED_PORT'],
          '-d', 'postgres', '-U', 'omm', '-v', 'ON_ERROR_STOP=1']
startup_lines = {'', 'Initializing crypto library', 'cryptoInitWrapper ok',
                 'Sophos_client ok', 'Mitra_client ok', 'FBdsse_client ok'}
legacy_startup = re.compile(
    r'(?:Error: Unable to open file for reading: |Dictionary loaded from file: )'
    r'/opt/og/openGauss-server/sse_storage/(?:fides_client|mitra_filecnt|mitra_srccnt)\.mat'
    r'|Dictionary size: [0-9]+')


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def execute(statement, protected=False, must_succeed=True):
    env = os.environ.copy()
    for key in ('SSE_V1_STATE_DIR', 'SSE_V1_MODE', 'STATE_FAULT', 'LD_PRELOAD'):
        env.pop(key, None)
    if protected:
        env['SSE_V1_STATE_DIR'] = str(state)
    result = subprocess.run(prefix + ['-c', statement.encode('utf-8')], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output = [line for line in result.stdout.decode('utf-8').splitlines()
              if line not in startup_lines and not legacy_startup.fullmatch(line)]
    error = result.stderr.decode('utf-8', errors='replace')
    if must_succeed:
        check(result.returncode == 0, 'SQL execution failed: ' + error)
    return output, result.returncode, error


def query(statement, protected=False):
    return execute(statement, protected)[0]


def quote(value):
    return "'" + value.replace("'", "''") + "'"


def loaded_libraries(executable, names):
    output = subprocess.check_output(['ldd', str(executable)]).decode('utf-8')
    check('not found' not in output, 'unresolved library for ' + executable.name)
    paths = {}
    for line in output.splitlines():
        words = line.split()
        if len(words) >= 3 and words[1] == '=>' and words[0] in names:
            expected = repo/'dest/lib'/words[0]
            check(Path(words[2]).samefile(expected), 'library outside candidate installation: ' + words[0])
            paths[words[0]] = str(expected.relative_to(repo))
    check(set(paths) == set(names), 'missing expected library for ' + executable.name)
    return paths


def verify_native():
    version = query('SELECT version();')
    check(len(version) == 1 and 'build 5c484c59' in version[0], 'unexpected server SQL version: ' + repr(version))
    check(query('SHOW data_directory;') == [str(run/'db/instance')], 'wrong backend data directory')
    check(query("SELECT 'ordinary' || '|' || (1+1)::text;") == ['ordinary|2'], 'ordinary SQL result differs')
    libraries = ('libsse_crypto.so', 'libmitra_sse_crypto.so', 'libcrypto.so.1.1')
    return {'actual_server_version': version[0], 'ordinary_sql': 'ordinary|2',
            'gsql_libraries': loaded_libraries(gsql, libraries + ('libpq_ce.so.5',)),
            'gaussdb_libraries': loaded_libraries(repo/'dest/bin/gaussdb', libraries)}


def verify_v1():
    table = 'shy05_short'
    data = [('01', 'a', '单字 词'), ('02', 'a', ''), ('03', 'ab', '前缀区分'),
            ('04', '中', '中文 短词'), ('05', '中文', '双字前缀')]
    query("CREATE TABLE shy05_short (ind text, keyword text SSE='FIDES', payload text) "
          "WITH (sse_storage='ROW_AEAD_V1');", True)
    values = ','.join('(' + ','.join(quote(value) for value in row) + ')' for row in data)
    query('INSERT INTO shy05_short VALUES ' + values + ';', True)
    queries = {}
    for word in ('a', 'ab', '中', '中文', 'a?', 'a'):
        result = query('SELECT * FROM shy05_short WHERE keyword=' + quote(word) + ';', True)
        expected = ['|'.join(row) for row in data if row[1] == word]
        check(Counter(result) == Counter(expected), 'complete rows differ for ' + repr(word) + ': ' + repr(result))
        queries[word] = sorted(result)
    columns = query("SELECT table_name||':'||string_agg(column_name,',' ORDER BY ordinal_position) "
                    "FROM information_schema.columns WHERE table_schema='public' AND "
                    "table_name IN ('shy05_short_data','shy05_short_index') GROUP BY table_name ORDER BY table_name;")
    check(columns == ['shy05_short_data:rid,data_blob', 'shy05_short_index:ut,locator_blob'],
          'unexpected physical columns: ' + repr(columns))
    check(query("SELECT logical_schema||'|'||logical_table||'|'||protocol_version::text "
                "FROM public.sse_v1_catalog WHERE logical_table='shy05_short';") == ['public|shy05_short|1'],
          'version-1 registration missing')

    physical_sql = ("SELECT rid||'|'||data_blob FROM public.shy05_short_data ORDER BY rid;",
                    "SELECT ut||'|'||locator_blob FROM public.shy05_short_index ORDER BY ut;")
    business, index = (query(statement) for statement in physical_sql)
    rids = []
    for line in business:
        rid, blob = line.split('|', 1)
        check(re.fullmatch('[0-9a-f]{32}', rid) and re.search('[a-f]', rid), 'expected non-integer rid')
        check(base64.b64decode(blob, validate=True).startswith(b'SSE1\1\1'), 'not a row envelope')
        rids.append(rid)
    for line in index:
        token, blob = line.split('|', 1)
        check(base64.b64decode(token, validate=True), 'empty update token')
        check(base64.b64decode(blob, validate=True).startswith(b'SSE1\1\2'), 'not a locator envelope')
    check(len(rids) == len(set(rids)) == len(index) == len(data), 'physical identity/count differs')

    def state_snapshot():
        return {file.name: (file.stat().st_mode, file.read_bytes()) for file in state.iterdir()
                if file.is_file()}

    baseline = state_snapshot()
    unsupported = {
        'update': "UPDATE shy05_short SET payload='changed' WHERE keyword='a';",
        'delete': "DELETE FROM shy05_short WHERE keyword='a';",
        'join': 'SELECT * FROM shy05_short JOIN shy05_short_data ON true;',
        'range': "SELECT * FROM shy05_short WHERE keyword > 'a';",
        'like': "SELECT * FROM shy05_short WHERE keyword LIKE 'a%';",
        'or': "SELECT * FROM shy05_short WHERE keyword='a' OR keyword='ab';",
        'dollar_quote': "INSERT INTO shy05_short VALUES ('06',$$aa$$,'x');",
        'escape_quote': "INSERT INTO shy05_short VALUES ('06',E'xx','x');",
    }
    rejected = {}
    for name, statement in unsupported.items():
        output, rc, error = execute(statement, protected=True, must_succeed=False)
        check(rc != 0 and not output, name + ' was accepted or produced output: ' + repr(output))
        match = re.search(r'Fides (unsupported|invalid_input):', error)
        check(match, name + ' did not explicitly reject protected SQL: ' + error)
        check(state_snapshot() == baseline and tuple(query(sql) for sql in physical_sql) == (business, index),
              name + ' changed state or stored records')
        rejected[name] = {'exit_code': rc, 'error': match.group(1), 'business_rows': 0}
    return {'complete_rows_by_keyword': queries, 'physical_columns': columns,
            'non_integer_unique_rids': len(rids), 'catalog_protocol_version': 1,
            'rejected_scoped_sql': rejected, 'state_and_database_unchanged_after_rejection': True}


def verify_legacy():
    apple = ['1|shy01_apple|red apple', '2|shy01_apple|green apple']
    banana = ['3|shy01_banana|yellow banana']
    checks = [('shy01_apple', apple), ('shy01_banana', banana), ('shy01_apple', apple)]
    for word, expected in checks:
        result = query('SELECT * FROM shy01_demo WHERE keyword=' + quote(word) + ';')
        check(Counter(result) == Counter(expected), 'legacy full row result differs: ' + repr(result))

    # ZYC-01 already guarded legacy unknown words on the SHY-04 baseline.
    # ROW_AEAD_V1 has a distinct supported empty-result path.
    counters = run/'client/fides_client.mat'
    mapping = run/'client/sse_table.mat'
    index_sql = "SELECT en_ind||'|'||ut FROM public.shy01_demo_index ORDER BY ut;"
    before = (counters.read_bytes(), mapping.read_bytes(), query(index_sql))
    output, rc, error = execute("SELECT * FROM shy01_demo WHERE keyword='shy01_unknown';",
                                must_succeed=False)
    check(rc != 0 and not output and
          'no search token was generated and the request was not sent' in error,
          'legacy unknown-keyword guard changed: ' + error)
    check(before == (counters.read_bytes(), mapping.read_bytes(), query(index_sql)),
          'legacy unknown keyword changed the counters, mapping or index')
    return {'legacy_fides_full_rows': {'shy01_apple': apple, 'shy01_banana': banana},
            'successful_gsql_searches': len(checks),
            'legacy_unknown_keyword': {'exit_code': rc, 'business_rows': 0,
                                       'request_not_sent': True, 'state_and_index_unchanged': True}}


mode = sys.argv[1]
check(mode in ('row-v1', 'legacy'), 'unknown native fixture')
result = verify_native()
result.update(verify_v1() if mode == 'row-v1' else verify_legacy())
result.update({'mode': mode, 'result': 'PASS'})
(run / ('evidence/shy05-' + mode + '.json')).write_text(
    json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('SHY05_NATIVE_' + mode + '=PASS', flush=True)
