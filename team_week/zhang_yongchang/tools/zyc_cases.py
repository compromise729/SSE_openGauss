#!/usr/bin/env python3
"""Exercise actual gsql -> state/crypto/Fides -> isolated SQL request paths."""
import json
import base64
from collections import Counter
from zyc_checks import validate_request_coverage
import os
import shutil
import stat
import subprocess
from pathlib import Path

run=Path(os.environ['ISOLATED_RUN_DIR'])
repo=Path(os.environ['OPENGAUSS_PROJECT_ROOT'])
build=Path(os.environ['LIVE_BUILD_DIR'])
state=run/'evidence/v1-state'
output=run/'evidence/live';output.mkdir(mode=0o700)
common=['-X','-q','-A','-t','-F','|','-h',str(run/'socket'),'-p',os.environ['ISOLATED_PORT'],
        '-d','postgres','-U','omm','-v','ON_ERROR_STOP=1']
records=[];sequence=0

def require(ok,message):
    if not ok:raise RuntimeError(message)

def raw(sql):
    env=dict(os.environ)
    for key in ('SSE_V1_STATE_DIR','SSE_V1_MODE','LD_PRELOAD','STATE_FAULT'):env.pop(key,None)
    p=subprocess.run([str(repo/'dest/bin/gsql')]+common+['-c',sql.encode('utf-8')],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    require(p.returncode==0,'inspection SQL failed: '+p.stderr.decode(errors='replace'))
    return p.stdout.decode(errors='replace').splitlines()

def snapshot_server():
    lines=raw("SELECT 'SNAP='||(SELECT count(*) FROM public.week_demo_data)||':'||"
              "(SELECT count(*) FROM public.week_demo_index)||':'||"
              "COALESCE((SELECT md5(string_agg(rid||data_blob,',' ORDER BY rid)) FROM public.week_demo_data),'-')||':'||"
              "COALESCE((SELECT md5(string_agg(ut||locator_blob,',' ORDER BY ut)) FROM public.week_demo_index),'-');")
    return next(s for s in lines if s.startswith('SNAP='))

def snapshot_files(directory):
    return {p.name:(stat.S_IMODE(p.stat().st_mode),p.read_bytes()) for p in directory.iterdir() if p.is_file()}

def client(sql,directory=None,fault=None):
    global sequence
    sequence+=1
    directory=directory or state
    env=dict(os.environ,SSE_V1_STATE_DIR=str(directory),STATE_TRACE='1',LD_PRELOAD=str(build/'live-fault-hook.so'))
    if fault:env['STATE_FAULT']=fault
    else:env.pop('STATE_FAULT',None)
    p=subprocess.run([str(build/'gsql')]+common+['-c',sql.encode('utf-8')],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    (output/('request-%02d.stdout'%sequence)).write_bytes(p.stdout)
    (output/('request-%02d.stderr'%sequence)).write_bytes(p.stderr)
    events=[s[len('STATE_EVENT '):] for s in p.stderr.decode(errors='replace').splitlines() if s.startswith('STATE_EVENT ')]
    return p,events

def succeeded(p):require(p.returncode==0,p.stderr.decode(errors='replace'))
def ordered(events,phases):
    positions=[events.index(p) for p in phases]
    require(positions==sorted(positions),'wrong request event order: '+repr(events))
def count(keyword):
    lines=(state/'fides_client.mat').read_text().splitlines()
    return next(int(line.rsplit(' ',1)[1]) for line in lines if line.rsplit(' ',1)[0]==keyword)
def apple_result(p):
    rows=[s for s in p.stdout.decode().splitlines() if s.startswith(('1|apple|','2|apple|'))]
    require(sorted(rows)==['1|apple|red apple','2|apple|green apple'],'wrong/missing/repeated authenticated rows: '+repr(rows))
def copy_state(name):
    target=run/'evidence'/name;shutil.copytree(str(state),str(target));return target
def no_business_events(events):
    require(not any(e in events for e in ('begin_durable','counter_advanced','search_take_sent','insert_sent','reinsert_sent','fetch_sent')),
            'unexpected request/counter event: '+repr(events))

def case(name,action):
    item={'case':name,'result':'FAIL'};start=sequence
    try:
        item.update(action() or {});item['result']='PASS'
        print('PASS '+name,flush=True)
    except Exception as e:
        item['reason']=str(e);print('FAIL '+name+': '+str(e),flush=True)
        raise
    finally:
        item['requests']=list(range(start+1,sequence+1));records.append(item)
        (output/'results.json').write_text(json.dumps({'cases':records,'result':'PASS' if all(x['result']=='PASS' for x in records) else 'FAIL',
            'request_path':'actual gsql SendQuery -> execute_request -> real Fides -> isolated database'},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def create():
    p,e=client("CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text)\n"
               "WITH (sse_storage='ROW_AEAD_V1');");succeeded(p)
    ordered(e,['keys_loaded','begin_durable','create_sent','state_saved','complete_durable'])
    require(not (state/'operation.v1').exists(),'create marker not cleared')
    return {'events':e}
def insert():
    p,e=client("INSERT INTO week_demo VALUES ('1','apple','red apple'),('2','apple','green apple'),('3','banana','yellow banana');")
    succeeded(p);ordered(e,['begin_durable','counter_advanced','insert_sent','state_saved','complete_durable'])
    require(count('apple')==2 and count('banana')==1,'wrong real insertion counters')
    require(snapshot_server().startswith('SNAP=3:3:'),'wrong physical row count')
    return {'events':e,'apple_counter':count('apple'),'database_rows':3,'index_rows':3}
def search():
    before=count('apple')
    keys_before={p.name:p.read_bytes() for p in state.glob('*.key*')}
    p,e=client("SELECT * FROM week_demo WHERE keyword='apple';");succeeded(p);apple_result(p)
    ordered(e,['preflight','begin_durable','token_generated','search_take_sent','counter_advanced','reinsert_sent',
               'fetch_sent','rows_authenticated','search_commit','state_saved','complete_durable'])
    require(count('apple')==before+2,'wrong real search/reinsert counter change')
    require(snapshot_server().startswith('SNAP=3:3:'),'search changed business/index row counts')
    require(keys_before=={p.name:p.read_bytes() for p in state.glob('*.key*')},'real keys changed across process reopen')
    require(not (state/'operation.v1').exists(),'search marker not cleared')
    return {'events':e,'counter_before':before,'counter_after':count('apple'),'rows':['1','2'],'key_bytes_unchanged':True}

_base_client = client
issued_requests = []

def client(sql, directory=None, fault=None, expected_kind=None):
    number = sequence + 1
    path = output / ('request-%02d.wire.jsonl' % number)
    saved = {name: os.environ.get(name) for name in ('ZYC_TRACE_FILE', 'ZYC_PUBLIC_FIXTURE')}
    os.environ.update(ZYC_TRACE_FILE=str(path), ZYC_PUBLIC_FIXTURE='1')
    try:
        p, events = _base_client(sql, directory, fault)
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
    if expected_kind is None:
        expected_kind = {'CREATE': 'create', 'INSERT': 'insert', 'SELECT': 'search'}.get(sql.lstrip().split()[0].upper(), 'unsupported')
    issued_requests.append({'request': number, 'exit': p.returncode, 'events': events, 'expected_kind': expected_kind})
    return p, events

def independently_decode(kind, values):
    p = subprocess.run([str(build/'zyc-decode'), str(state), 'week_demo', kind],
                       input=('\n'.join(values)+'\n').encode('utf-8'),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(p.returncode == 0, p.stderr.decode(errors='replace'))
    return [json.loads(line) for line in p.stdout.decode('utf-8').splitlines()]

def quote(value):
    return "'" + value.replace("'", "''") + "'"

def rows(p):
    return Counter(line for line in p.stdout.decode('utf-8').splitlines() if '|' in line)

def expect_rows(sql, expected, directory=None):
    p, events = client(sql, directory, expected_kind='search' if expected else 'unknown')
    succeeded(p)
    require(rows(p) == Counter('|'.join(row) for row in expected), 'complete row multiset differs')
    return events

def physical_ciphertext():
    data = [s[5:].split('|') for s in raw("SELECT 'DATA='||rid||'|'||data_blob FROM public.week_demo_data;") if s.startswith('DATA=')]
    index = [s[4:].split('|') for s in raw("SELECT 'IDX='||ut||'|'||locator_blob FROM public.week_demo_index;") if s.startswith('IDX=')]
    require(len(data) == len(index) == 3, 'wrong initial physical row count')
    for rid, blob in data:
        require(len(rid) == 32 and all(c in '0123456789abcdef' for c in rid), 'rid shape')
        require(base64.b64decode(blob, validate=True)[:6] == b'SSE1\x01\x01', 'row envelope shape')
    for ut, blob in index:
        decoded = base64.b64decode(blob, validate=True)
        require(len(decoded) == 71 and decoded[:6] == b'SSE1\x01\x02', 'locator envelope shape')
    plain = independently_decode('row', [rid+'\t'+blob for rid, blob in data])
    locators = independently_decode('locator', [blob for ut, blob in index])
    require(Counter((x['ind'], x['keyword'], x['payload']) for x in plain) ==
            Counter([('1','apple','red apple'), ('2','apple','green apple'), ('3','banana','yellow banana')]),
            'independent row decode differs from public input')
    require(Counter(x['rid'] for x in locators) == Counter(rid for rid, blob in data) and
            all(x['operation'] == 0 for x in locators), 'independent locator decode mismatch')
    (output/'independent-decode.json').write_text(json.dumps({'rows': plain, 'locators': locators},
        ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return {'physical_data_rows': 3, 'physical_index_rows': 3, 'authenticated_envelope_purposes': [1, 2],
            'independent_row_and_locator_decode': True}

def baseline_queries():
    apple = [('1','apple','red apple'), ('2','apple','green apple')]
    events = expect_rows("SELECT * FROM week_demo WHERE keyword='apple';", apple)
    expect_rows("SELECT * FROM week_demo WHERE keyword='banana';", [('3','banana','yellow banana')])
    expect_rows("SELECT * FROM week_demo WHERE keyword='apple';", apple)
    before, server = snapshot_files(state), snapshot_server()
    expect_rows("SELECT * FROM week_demo WHERE keyword='review_unknown';", [])
    require(before == snapshot_files(state) and server == snapshot_server(), 'unknown query changed state')
    return {'events': events, 'apple': 2, 'banana': 1, 'unknown': 0, 'reopened_processes': True}

def append_and_repeat():
    global appended_rid
    before_rids = set(s[4:] for s in raw("SELECT 'RID='||rid FROM public.week_demo_data;") if s.startswith('RID='))
    p, _ = client("INSERT INTO week_demo VALUES ('4','apple','new apple');")
    succeeded(p)
    after_rids = set(s[4:] for s in raw("SELECT 'RID='||rid FROM public.week_demo_data;") if s.startswith('RID='))
    require(len(after_rids-before_rids) == 1, 'append did not create one independent rid')
    appended_rid = next(iter(after_rids-before_rids))
    expected = [('1','apple','red apple'), ('2','apple','green apple'), ('4','apple','new apple')]
    for _ in range(3):
        expect_rows("SELECT * FROM week_demo WHERE keyword='apple';", expected)
    require(snapshot_server().startswith('SNAP=4:4:'), 'search reinsert changed business rows')
    return {'repeated_searches': 3, 'apple_rows_after_append': 3, 'physical_business_rows': 4}

def strings_and_empty():
    values = [('中文编号','中文 苹果 space','前后 空格 中文'),
              ('quoted', "it's AND OR not", "o'brien apple"), ('','','')]
    sql = 'INSERT INTO week_demo VALUES ' + ','.join('('+','.join(quote(x) for x in row)+')' for row in values) + ';'
    p, _ = client(sql); succeeded(p)
    for row in values:
        expect_rows('SELECT * FROM week_demo WHERE keyword='+quote(row[1])+';', [row])
    return {'utf8': True, 'spaces': True, 'sql_doubled_quotes': True, 'quoted_AND_OR': True, 'empty_fields': True}

def maximum_row():
    row = ('i'*16384, 'k'*1024, 'p'*16384)
    p, _ = client('INSERT INTO week_demo VALUES ('+','.join(quote(x) for x in row)+');'); succeeded(p)
    expect_rows('SELECT * FROM week_demo WHERE keyword='+quote(row[1])+';', [row])
    sizes = raw('SELECT \'LEN=\'||max(octet_length(data_blob)) FROM public.week_demo_data;')
    require('LEN=45144' in sizes, 'maximum legal row envelope truncated')
    return {'field_bytes': [16384, 1024, 16384], 'base64_bytes': 45144, 'exact_roundtrip': True}

def reject_before_effects():
    inputs = ["INSERT INTO week_demo VALUES ('x','" + 'k'*1025 + "','p');",
              "INSERT INTO week_demo VALUES ('" + 'i'*16385 + "','x','p');",
              "INSERT INTO week_demo VALUES ('x','x','" + 'p'*16385 + "');",
              "INSERT INTO week_demo VALUES ('ok','apple','ok'),('bad',NULL,'bad');",
              "SELECT * FROM week_demo WHERE keyword='apple' AND payload='red apple';",
              "SELECT ind FROM week_demo WHERE keyword='apple';"]
    for sql in inputs:
        before, server = snapshot_files(state), snapshot_server()
        p, events = client(sql)
        require(p.returncode != 0, 'unsupported/oversized input accepted')
        require(not rows(p), 'rejected request displayed business rows')
        no_business_events(events)
        require(before == snapshot_files(state) and server == snapshot_server(), 'rejected request had effects')
    return {'rejections': len(inputs), 'state_and_database_unchanged': True, 'no_partial_batch': True}

def stopped_failure(p, events, directory, error):
    require(p.returncode != 0 and error in p.stderr, 'required failure did not propagate')
    require(not rows(p), 'partial plaintext displayed')
    require((directory/'operation.v1').exists(), 'failure marker missing')
    require('rows_authenticated' not in events and 'search_commit' not in events, 'failed operation committed')
    again, after = client("SELECT * FROM week_demo WHERE keyword='apple';", directory)
    require(again.returncode != 0 and b'incomplete_operation' in again.stderr, 'reopened client not blocked')
    no_business_events(after)

def insert_failure():
    target = copy_state('review-insert-failure')
    size = int(next(s[6:] for s in raw("SELECT 'COUNT='||count(*) FROM public.week_demo_data;") if s.startswith('COUNT=')))
    raw("CREATE FUNCTION public.review_fail_insert() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
        "IF (SELECT count(*) FROM public.week_demo_data) > %d THEN RAISE EXCEPTION 'review injected second-row failure'; "
        "END IF; RETURN NEW; END; $$;" % size)
    raw("CREATE TRIGGER review_fail_insert BEFORE INSERT ON public.week_demo_data FOR EACH ROW EXECUTE PROCEDURE public.review_fail_insert();")
    server = snapshot_server()
    try:
        p, events = client("INSERT INTO week_demo VALUES ('first','apple','must rollback first'),('second','apple','injected failure');", target)
        require(p.returncode != 0 and b'db_failed' in p.stderr, 'write failure returned success')
        require((target/'operation.v1').exists() and server == snapshot_server(), 'write failure not atomic/marked')
        again, events = client("SELECT * FROM week_demo WHERE keyword='apple';", target)
        require(again.returncode != 0 and b'incomplete_operation' in again.stderr, 'write failure restart not blocked')
    finally:
        raw('DROP TRIGGER review_fail_insert ON public.week_demo_data; DROP FUNCTION public.review_fail_insert();')
    return {'second_row_failure_rolls_back_entire_batch': True, 'write_nonzero': True,
            'database_unchanged': True, 'marker_retained': True, 'reopen_blocked': True}

def last_row_authentication():
    target = copy_state('review-auth-failure')
    raw('ALTER FUNCTION public.sse_v1_fetch(text,text,text[]) RENAME TO review_fetch_saved;')
    # Real fetch is retained; sort its output and alter only the final row's tag.
    raw("""CREATE FUNCTION public.sse_v1_fetch(text,text,text[])
        RETURNS TABLE(rid text,data_blob text) LANGUAGE sql STABLE AS $$
        SELECT rid, CASE WHEN row_number() OVER(ORDER BY rid)=count(*) OVER() THEN
          replace(encode(set_byte(decode(data_blob,'base64'),octet_length(decode(data_blob,'base64'))-1,
            (get_byte(decode(data_blob,'base64'),octet_length(decode(data_blob,'base64'))-1)+1)%256),
            'base64'),chr(10),'') ELSE data_blob END
        FROM public.review_fetch_saved($1,$2,$3) ORDER BY rid $$;""")
    server = snapshot_server()
    try:
        p, events = client("SELECT * FROM week_demo WHERE keyword='apple';", target)
        stopped_failure(p, events, target, b'auth_failed')
        require(server == snapshot_server(), 'authentication failure did not roll back search')
    finally:
        raw('DROP FUNCTION public.sse_v1_fetch(text,text,text[]); ALTER FUNCTION public.review_fetch_saved(text,text,text[]) RENAME TO sse_v1_fetch;')
    return {'events': events, 'healthy_rows_before_last': 2, 'last_returned_tag_tampered': True,
            'no_partial_plaintext': True, 'database_unchanged': True, 'reopen_blocked': True}

def missing_business_row():
    target = copy_state('review-missing-row')
    saved = next(s[5:].split('|') for s in raw("SELECT 'SAVE='||rid||'|'||data_blob FROM public.week_demo_data WHERE rid="+quote(appended_rid)+';') if s.startswith('SAVE='))
    raw('DELETE FROM public.week_demo_data WHERE rid='+quote(appended_rid)+';')
    server = snapshot_server()
    try:
        p, events = client("SELECT * FROM week_demo WHERE keyword='apple';", target)
        stopped_failure(p, events, target, b'inconsistent_result')
        require(server == snapshot_server(), 'inconsistent fetch did not roll back search')
    finally:
        raw('INSERT INTO public.week_demo_data(rid,data_blob) VALUES ('+quote(saved[0])+','+quote(saved[1])+');')
    return {'events': events, 'physical_business_row_deleted': True, 'no_partial_plaintext': True, 'reopen_blocked': True}

def verify_wire_phases():
    import re
    phases = []
    calls_by_request = validate_request_coverage(issued_requests, output)
    def array(value):
        require(value.startswith('{') and value.endswith('}'), 'invalid bound SQL array')
        return json.loads('['+value[1:-1]+']')
    for request in issued_requests:
        calls = calls_by_request[request['request']]
        for call in calls:
            require(not re.search(r'\bkeyword\s*=', call['sql'], re.I), 'plaintext keyword predicate sent')
            require('INSERT INTO week_demo VALUES' not in call['sql'], 'original business VALUES sent')
            if 'sse_v1_insert(' in call['sql']:
                require(len(call['params']) == 5, 'insert parameter shape')
                rids, blobs, uts, positions = map(array, call['params'][1:])
                require(len(rids) == len(blobs) == len(uts) == len(positions), 'insert array mismatch')
                require(all(base64.b64decode(b, validate=True)[:6] == b'SSE1\x01\x01' for b in blobs), 'non-row ciphertext bound')
                require(all(base64.b64decode(b, validate=True)[:6] == b'SSE1\x01\x02' for b in positions), 'non-locator ciphertext bound')
        searches = [c for c in calls if c['sql'].startswith('fides_v1_select ')]
        if request['exit'] or request['expected_kind'] != 'search':
            continue
        require(len(searches) == 1, 'wrong Fides search request count')
        search = searches[0]
        require(re.fullmatch(r'fides_v1_select \* from public\.week_demo_index where token=[A-Za-z0-9+/=]+ and derivation_key=<redacted> and add_count=[0-9]+;', search['sql']), 'unexpected Fides wire shape')
        recovered = independently_decode('locator', [row[1] for row in search['rows']])
        expected_rids = set(x['rid'] for x in recovered)
        require(all(x['operation'] == 0 for x in recovered), 'locator operation mismatch')
        reinsert = next(c for c in calls if 'sse_v1_reinsert(' in c['sql'])
        fetch = next(c for c in calls if 'sse_v1_fetch(' in c['sql'])
        requested = array(fetch['params'][1])
        returned = [row[0] for row in fetch['rows']]
        reinserted = independently_decode('locator', array(reinsert['params'][2]))
        require(set(requested) == expected_rids == set(returned) == set(x['rid'] for x in reinserted), 'phase rid sets differ')
        require(len(requested) == len(returned) == len(expected_rids), 'duplicate/missing fetched row')
        require(reinsert['status'] == 'PGRES_TUPLES_OK' and fetch['status'] == 'PGRES_TUPLES_OK', 'reinsert/fetch failed')
        phases.append({'request': request['request'], 'search_request': search['sql'],
                       'encrypted_locator_rows': search['rows'], 'authenticated_rids': sorted(expected_rids),
                       'reinsert_rids': sorted(x['rid'] for x in reinserted), 'fetch_rids': requested,
                       'returned_rids': returned, 'events': request['events']})
    expected_searches = sum(r['exit'] == 0 and r['expected_kind'] == 'search' for r in issued_requests)
    require(expected_searches > 0 and len(phases) == expected_searches, 'missing actual successful search phase traces')
    (output/'phases.json').write_text(json.dumps(phases, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (output/'requests.json').write_text(json.dumps(issued_requests, indent=2)+'\n', encoding='utf-8')
    return {'validated_searches': len(phases), 'business_sql_only_ciphertext': True,
            'no_plaintext_keyword_predicate': True, 'independent_locator_and_fetch_set_checks': True}

def registration_failure():
    target = copy_state('review-registration-failure')
    before = snapshot_files(state)
    raw("ALTER TABLE public.sse_v1_catalog ADD CONSTRAINT review_registration CHECK (logical_table <> 'week_demo');")
    sql = "CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text) WITH (sse_storage='ROW_AEAD_V1');"
    try:
        p, events = client(sql, target)
        require(p.returncode != 0 and b'db_failed' in p.stderr, 'registration failure returned success')
        require((target/'operation.v1').exists(), 'failed registration marker absent')
        require('META=0:0' in raw("SELECT 'META='||(SELECT count(*) FROM public.sse_v1_catalog)||':'||"
            "(SELECT count(*) FROM pg_class WHERE relname IN ('week_demo_data','week_demo_index'));"), 'failed registration left physical objects/catalog')
        again, _ = client(sql, target)
        require(again.returncode != 0 and b'incomplete_operation' in again.stderr, 'failed create not blocked on reopen')
        require(before == snapshot_files(state), 'create failure modified healthy state')
    finally:
        raw('ALTER TABLE public.sse_v1_catalog DROP CONSTRAINT review_registration;')
    return {'events': events, 'physical_create_and_catalog_rolled_back': True, 'marker_retained': True, 'reopen_blocked': True}

case('ZYC-00-registration-failure', registration_failure)
case('ZYC-01-create', create)
case('ZYC-02-insert-three', insert)
case('ZYC-03-physical-ciphertext', physical_ciphertext)
case('ZYC-04-normal-reopen-unknown', baseline_queries)
case('ZYC-05-append-and-repeat', append_and_repeat)
case('ZYC-06-utf8-quote-empty', strings_and_empty)
case('ZYC-07-maximum-payload', maximum_row)
case('ZYC-08-reject-before-effects', reject_before_effects)
case('ZYC-09-write-failure', insert_failure)
case('ZYC-10-last-row-authentication', last_row_authentication)
case('ZYC-11-missing-fetched-row', missing_business_row)
case('ZYC-12-wire-phases', verify_wire_phases)
print('ADDITIONAL_RESULT=PASS CASES=13', flush=True)
