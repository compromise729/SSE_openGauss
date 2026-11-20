#!/usr/bin/env python3
"""YZH-02 fault injection against the installed SHY-04 gsql and isolated server."""
import base64
import json
import os
import shutil
import subprocess
from pathlib import Path

repo = Path(os.environ['OPENGAUSS_PROJECT_ROOT'])
run = Path(os.environ['ISOLATED_RUN_DIR'])
state = run / 'evidence/v1-state'
out = run / 'evidence/yzh02'
out.mkdir(mode=0o700)
gsql = repo / 'dest/bin/gsql'
common = ['-X', '-q', '-A', '-t', '-F', '|', '-h', str(run / 'socket'),
          '-p', os.environ['ISOLATED_PORT'], '-d', 'postgres', '-U', 'omm',
          '-v', 'ON_ERROR_STOP=1']
results = []
request_no = 0
STARTUP_LINES = {'', 'Initializing crypto library', 'cryptoInitWrapper ok'}


def sql_quote(s):
    return "'" + s.replace("'", "''") + "'"


def call(sql, client_state=None):
    global request_no
    request_no += 1
    env = os.environ.copy()
    for key in ('SSE_V1_STATE_DIR', 'SSE_V1_MODE', 'LD_PRELOAD', 'STATE_FAULT'):
        env.pop(key, None)
    if client_state is not None:
        env['SSE_V1_STATE_DIR'] = str(client_state)
    p = subprocess.run([str(gsql)] + common + ['-c', sql.encode('utf-8')],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stem = out / ('request-%03d' % request_no)
    stem.with_suffix('.stdout').write_bytes(p.stdout)
    stem.with_suffix('.stderr').write_bytes(p.stderr)
    return p


def raw(sql):
    p = call(sql)
    if p.returncode:
        raise AssertionError('physical SQL failed: ' + p.stderr.decode(errors='replace'))
    return p.stdout.decode('utf-8').splitlines()


def client(sql, st=state):
    return call(sql, st)


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def case(name, fn):
    first = request_no + 1
    item = {'case': name, 'source_commit': os.environ['YZH02_SOURCE_BASELINE'],
            'checkout_head': os.environ['YZH02_CHECKOUT_HEAD']}
    try:
        item.update(fn() or {})
        item['result'] = 'PASS'
        print('PASS ' + name, flush=True)
    except Exception as exc:
        item.update(result='FAIL', reason=str(exc))
        print('FAIL ' + name + ': ' + str(exc), flush=True)
    finally:
        item['requests'] = list(range(first, request_no + 1))
        results.append(item)
        (out / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def decode(kind, values, table='week_demo'):
    p = subprocess.run([str(run / 'private/native/zyc-decode'), str(state), table, kind],
                       input=('\n'.join(values) + '\n').encode(), stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    require(p.returncode == 0, 'independent decode failed: ' + p.stderr.decode(errors='replace'))
    return [json.loads(s) for s in p.stdout.decode('utf-8').splitlines()]


def data_rows():
    lines = raw("SELECT 'D='||rid||'|'||data_blob FROM public.week_demo_data ORDER BY rid;")
    return [s[2:].split('|', 1) for s in lines if s.startswith('D=')]


def index_rows(table='week_demo'):
    lines = raw("SELECT 'I='||ut||'|'||locator_blob FROM public." + table + '_index ORDER BY ut;')
    return [s[2:].split('|', 1) for s in lines if s.startswith('I=')]


def snapshot():
    lines = raw("SELECT 'S='||md5((SELECT string_agg(rid||data_blob,',' ORDER BY rid) FROM public.week_demo_data)) || ':' || "
                "md5((SELECT string_agg(ut||locator_blob,',' ORDER BY ut) FROM public.week_demo_index));")
    return next(s[2:] for s in lines if s.startswith('S='))


def copy_state(label):
    path = run / 'evidence' / ('yzh02-' + label)
    shutil.copytree(str(state), str(path))
    return path


def business_lines(stdout):
    return [line for line in stdout.splitlines() if line not in STARTUP_LINES]


def failure(sql, st, expected):
    before = snapshot()
    p = client(sql, st)
    stdout = p.stdout.decode('utf-8', errors='replace')
    stderr = p.stderr.decode('utf-8', errors='replace')
    require(p.returncode != 0, 'client returned success')
    require(not business_lines(stdout), 'partial business output printed')
    require(expected in stderr, 'expected error missing: ' + expected + ' / ' + stderr[-500:])
    require(before == snapshot(), 'failed query changed physical tables')
    return {'exit_code': p.returncode, 'stderr_code': expected, 'stdout_business_rows': 0,
            'database_unchanged_by_query': True}


def mutate_blob(blob, kind):
    if kind == 'truncate':
        return blob[:-4]
    data = bytearray(base64.b64decode(blob, validate=True))
    offset = {'version': 4, 'key_id': 6, 'ciphertext': 38, 'tag': len(data)-1}[kind]
    data[offset] ^= 1
    return base64.b64encode(data).decode('ascii')


dup_keyword = 'yzh02_duplicate'
dup_sql = "SELECT * FROM week_demo WHERE keyword='" + dup_keyword + "';"
dup_rids = []


def duplicate_rows():
    before = {rid for rid, _ in data_rows()}
    row = ('同一编号', dup_keyword, '完整业务行 apple 空格')
    values = '(' + ','.join(sql_quote(x) for x in row) + ')'
    p = client('INSERT INTO week_demo VALUES ' + values + ',' + values + ';')
    require(p.returncode == 0, p.stderr.decode(errors='replace'))
    pairs = [(rid, blob) for rid, blob in data_rows() if rid not in before]
    require(len(pairs) == 2, 'duplicate insertion did not create two rows')
    dup_rids[:] = [rid for rid, _ in pairs]
    require(pairs[0][1] != pairs[1][1], 'identical full rows have equal envelopes')
    # A different rid changes the AAD tag by itself. Compare the nonce separately.
    nonce_offset = 4 + 1 + 1 + 16
    nonces = [base64.b64decode(blob, validate=True)[nonce_offset:nonce_offset + 12] for _, blob in pairs]
    require(all(len(nonce) == 12 for nonce in nonces) and nonces[0] != nonces[1],
            'identical full rows reused an AES-GCM nonce')
    plain = decode('row', [rid + '\t' + blob for rid, blob in pairs])
    require(len(plain) == 2 and all((x['ind'], x['keyword'], x['payload']) == row for x in plain),
            'duplicate full row did not restore exactly')
    p = client(dup_sql)
    require(p.returncode == 0, p.stderr.decode(errors='replace'))
    require(business_lines(p.stdout.decode('utf-8')) == ['|'.join(row)] * 2,
            'actual client did not return exactly both identical complete rows')
    (out / 'public-duplicate-example.json').write_text(json.dumps({
        'input_row': row, 'ciphertexts_differ': True, 'nonces_differ': True, 'restored_rows': plain,
        'ciphertexts': [blob for _, blob in pairs],
        'note': 'Public synthetic rows and their complete ciphertexts; keys are omitted.'
    }, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return {'two_ciphertexts_differ': True, 'nonces_differ': True, 'two_complete_rows_restored': True}


def text_rows():
    samples = [('  前后空格  ', 'yzh02_中文', "尾部 空格  "),
               ('', 'yzh02_empty', ''),
               ('q' * 16, 'yzh02_boundary', 'p' * 32),
               ('长' * 200, 'yzh02_long', '文' * 1000)]
    for row in samples:
        p = client('INSERT INTO week_demo VALUES (' + ','.join(sql_quote(x) for x in row) + ');')
        require(p.returncode == 0, p.stderr.decode(errors='replace'))
        p = client('SELECT * FROM week_demo WHERE keyword=' + sql_quote(row[1]) + ';')
        require(p.returncode == 0, p.stderr.decode(errors='replace'))
        require(business_lines(p.stdout.decode('utf-8')) == ['|'.join(row)],
                'text restoration differs')
    return {'cases': len(samples), 'utf8_space_empty_boundary_long': True}


def row_fault(kind):
    rid = max(dup_rids)
    old = next(blob for r, blob in data_rows() if r == rid)
    st = copy_state('row-' + kind)
    # The physical table rejects malformed envelopes on UPDATE. Temporarily
    # remove its shape check so the actual client can be tested on stored damage.
    raw('ALTER TABLE public.week_demo_data DROP CONSTRAINT week_demo_data_data_blob_check;')
    try:
        raw('UPDATE public.week_demo_data SET data_blob=' + sql_quote(mutate_blob(old, kind)) +
            ' WHERE rid=' + sql_quote(rid) + ';')
        expected = ('key_invalid' if kind == 'key_id' else
                    'protocol_invalid' if kind in ('version', 'truncate') else 'auth_failed')
        return failure(dup_sql, st, expected)
    finally:
        raw('UPDATE public.week_demo_data SET data_blob=' + sql_quote(old) +
            ' WHERE rid=' + sql_quote(rid) + ';')
        raw('ALTER TABLE public.week_demo_data ADD CONSTRAINT week_demo_data_data_blob_check '
            'CHECK (public.sse_v1_blob_valid(data_blob, 1));')


def cross_row():
    pairs = [(rid, blob) for rid, blob in data_rows() if rid in dup_rids]
    require(len(pairs) == 2, 'duplicate rows missing')
    st = copy_state('cross-row')
    raw('UPDATE public.week_demo_data SET data_blob=' + sql_quote(pairs[1][1]) +
        ' WHERE rid=' + sql_quote(pairs[0][0]) + ';')
    try:
        return failure(dup_sql, st, 'auth_failed')
    finally:
        raw('UPDATE public.week_demo_data SET data_blob=' + sql_quote(pairs[0][1]) +
            ' WHERE rid=' + sql_quote(pairs[0][0]) + ';')


def locator_fault(kind):
    pairs = index_rows()
    st = copy_state('locator-' + kind)
    raw('ALTER TABLE public.week_demo_index DROP CONSTRAINT week_demo_index_locator_blob_check;')
    try:
        for ut, blob in pairs:
            raw('UPDATE public.week_demo_index SET locator_blob=' + sql_quote(mutate_blob(blob, kind)) +
                ' WHERE ut=' + sql_quote(ut) + ';')
        expected = ('key_invalid' if kind == 'key_id' else
                    'protocol_invalid' if kind in ('version', 'truncate') else 'auth_failed')
        return failure(dup_sql, st, expected)
    finally:
        for ut, blob in pairs:
            raw('UPDATE public.week_demo_index SET locator_blob=' + sql_quote(blob) +
                ' WHERE ut=' + sql_quote(ut) + ';')
        raw('ALTER TABLE public.week_demo_index ADD CONSTRAINT week_demo_index_locator_blob_check '
            'CHECK (public.sse_v1_blob_valid(locator_blob, 2));')


def key_fault(kind):
    st = copy_state('key-' + kind)
    path = st / 'row_aead_v1.key'
    key = bytearray(path.read_bytes())
    if kind == 'missing':
        path.unlink()
    elif kind == 'short':
        path.write_bytes(key[:8])
    else:
        key[0] ^= 1
        path.write_bytes(key)
    expected = 'key_invalid' if kind != 'wrong' else 'auth_failed'
    injected_key = path.read_bytes() if path.exists() else None
    result = failure(dup_sql, st, expected)
    actual_key = path.read_bytes() if path.exists() else None
    require(actual_key == injected_key, 'key bytes or presence changed after failure')
    return dict(result, key_unchanged=True)


def locator_example():
    pairs = index_rows()
    decoded = decode('locator', [blob for _, blob in pairs])
    rids = {rid for rid, _ in data_rows()}
    require(decoded and len(decoded) == len(pairs) and
            all(set(x) == {'rid', 'operation'} and x['operation'] == 0 and x['rid'] in rids for x in decoded),
            'locator content mismatch')
    (out / 'public-locator-example.json').write_text(json.dumps({
        'locator_blob': pairs[0][1], 'recovered': decoded[0],
        'explanation': 'This authenticates a location; business fields require a separate row decryption.'
    }, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return {'authenticated_locations': len(decoded), 'business_fields_not_in_locator': True}


def cross_table_locator():
    original = index_rows()
    location = decode('locator', [original[0][1]])[0]
    minted = subprocess.run([str(run / 'private/native/mint-foreign-locator'), str(state),
                             'yzh02_other', location['rid']], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    require(minted.returncode == 0, 'foreign locator fixture generation failed')
    foreign = minted.stdout.decode('ascii').strip()
    require(foreign != original[0][1], 'foreign scope fixture equals original')
    st = copy_state('locator-cross-table')
    try:
        for ut, _ in original:
            raw('UPDATE public.week_demo_index SET locator_blob=' + sql_quote(foreign) +
                ' WHERE ut=' + sql_quote(ut) + ';')
        return failure(dup_sql, st, 'auth_failed')
    finally:
        for ut, blob in original:
            raw('UPDATE public.week_demo_index SET locator_blob=' + sql_quote(blob) +
                ' WHERE ut=' + sql_quote(ut) + ';')


case('N01-duplicate-full-row', duplicate_rows)
case('N02-supported-text', text_rows)
case('N03-locator-example', locator_example)
for kind in ('ciphertext', 'tag', 'version', 'key_id', 'truncate'):
    case('R-' + kind, lambda k=kind: row_fault(k))
case('R-cross-record', cross_row)
for kind in ('ciphertext', 'tag', 'version', 'key_id', 'truncate'):
    case('L-' + kind, lambda k=kind: locator_fault(k))
for kind in ('wrong', 'missing', 'short'):
    case('K-' + kind, lambda k=kind: key_fault(k))
case('L-cross-table', cross_table_locator)
passed = len(results) == 18 and all(item['result'] == 'PASS' for item in results)
print('YZH02_RESULT=' + ('PASS' if passed else 'FAIL') + ' CASES=' + str(len(results)), flush=True)
if not passed:
    raise SystemExit(1)
