import os,json,subprocess,shutil,time,fcntl,hashlib
from pathlib import Path
import stat,errno
run=Path(os.environ['ISOLATED_RUN_DIR']);repo=Path(os.environ['OPENGAUSS_PROJECT_ROOT']);build=Path(os.environ['LZM04_BUILD'])
out=run/'evidence/lzm04';out.mkdir(mode=0o700);db='postgres';state=None;seq=0;results=[]
private=run/'private/lzm04';private.mkdir(mode=0o700)
commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=str(repo)).decode('utf-8').strip()
EXPECTED_CASES = 36
STARTUP_LINES = {'', 'Initializing crypto library', 'cryptoInitWrapper ok'}
APPLE_ROWS = ['1|apple|red apple', '2|apple|green apple']
raw_records=[];request_records={};snapseq=0;initialization={}
conn=['-X','-q','-A','-t','-F','|','-h',str(run/'socket'),'-p',os.environ['ISOLATED_PORT'],'-U','omm','-v','ON_ERROR_STOP=1']
def need(x,msg):
    if not x:raise RuntimeError(msg)
def raw(sql):
    p=subprocess.run([str(repo/'dest/bin/gsql')]+conn+['-d',db,'-c',sql.encode('utf8')],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    raw_records.append(dict(command=[str(repo/'dest/bin/gsql')]+conn+['-d',db,'-c',sql],exit_code=p.returncode,stdout=p.stdout.decode(errors='replace'),stderr=p.stderr.decode(errors='replace')))
    need(p.returncode==0,p.stderr.decode(errors='replace'));return p.stdout.decode().splitlines()
def query(sql,fault=None,async_=False):
    global seq
    seq+=1;name='request-%03d'%seq
    env=dict(os.environ,SSE_V1_STATE_DIR=str(state),LD_PRELOAD=str(build/'faults04.so'),STATE_SIGNAL_DIR=str(out/'signals'))
    if fault:env['STATE_FAULT']=fault
    else:env.pop('STATE_FAULT',None)
    a=(out/(name+'.stdout')).open('wb');b=(out/(name+'.stderr')).open('wb')
    p=subprocess.Popen([str(build/'gsql')]+conn+['-d',db,'-c',sql.encode('utf8')],env=env,stdout=a,stderr=b)
    a.close();b.close()
    record=dict(commit=commit,command=[str(build/'gsql')]+conn+['-d',db,'-c',sql],environment={k:env[k] for k in ('SSE_V1_STATE_DIR','LD_PRELOAD','STATE_SIGNAL_DIR')},fault=fault,stdout=name+'.stdout',stderr=name+'.stderr')
    if fault:record['environment']['STATE_FAULT']=fault
    request_records[seq]=(p,record)
    if async_:return p,name
    p.wait(timeout=90);return p.returncode,(out/(name+'.stdout')).read_text(encoding='utf8'),(out/(name+'.stderr')).read_text(encoding='utf8')
def good(sql):
    rc,stdout,stderr=query(sql);need(rc==0,stderr);return stdout,stderr
def database_snapshot():
    sql = ("SELECT 'SNAP='||(SELECT count(*) FROM week_demo_data)||':'||"
           "(SELECT count(*) FROM week_demo_index)||':'||"
           "COALESCE((SELECT md5(string_agg(rid||data_blob,',' ORDER BY rid)) FROM week_demo_data),'-')||':'||"
           "COALESCE((SELECT md5(string_agg(ut||locator_blob,',' ORDER BY ut)) FROM week_demo_index),'-');")
    return next(line for line in raw(sql) if line.startswith('SNAP='))
def count():return int((state/'fides_client.mat').read_text().strip().rsplit(' ',1)[1])
def files():
    global snapseq
    data={p.name:p.read_bytes() for p in state.iterdir() if p.is_file()};snapseq+=1
    # Key digests stay in the private run directory and are never copied to delivery.
    manifest={n:dict(sha256=hashlib.sha256(v).hexdigest(),mode=oct(stat.S_IMODE((state/n).stat().st_mode)),uid=(state/n).stat().st_uid) for n,v in data.items()}
    path=private/('snapshot-%03d.json.private'%snapseq);path.write_text(json.dumps(dict(state_dir=str(state),files=manifest),indent=2));path.chmod(0o600)
    return data

def metadata():
    if state is None or not state.exists():return {}
    return dict(directory_mode=oct(stat.S_IMODE(state.stat().st_mode)),files={p.name:dict(size=p.stat().st_size,mode=oct(stat.S_IMODE(p.stat().st_mode)),uid=p.stat().st_uid) for p in state.iterdir() if p.is_file()},marker_present=(state/'operation.v1').exists())

def initial_consistency(create):
    env=(state/'environment.v1').read_text().splitlines();parts=env[1].split('\t')
    oid=next(x[4:] for x in raw("SELECT 'OID='||oid::text FROM pg_database WHERE datname=current_database();") if x.startswith('OID='))
    lengths={'row_aead_v1.key':32,'locator_aead_v1.key':32,'row_aead_v1.keyid':16}
    good_keys=all((state/n).stat().st_size==length and stat.S_IMODE((state/n).stat().st_mode)==0o600 and (state/n).stat().st_uid==os.geteuid() for n,length in lengths.items())
    need(good_keys and env[0]=='SSE_ROW_ENVIRONMENT_V1' and parts[0]==(state/'deployment.v1').read_bytes().hex() and parts[1]==oid and parts[2]==str(run/'db/instance').encode().hex() and parts[3]==(state/'row_aead_v1.keyid').read_bytes().hex(),'initial environment/key mismatch')
    local=(state/'row_aead_tables.v1').read_text().splitlines();need(local[0]=='SSE_ROW_TABLES_V1','initial version header')
    expected='week_demo ind|undefined keyword|fides payload|undefined\n' if create else ''
    need((state/'sse_table.mat').read_text()==expected,'initial mapping mismatch')
    catalog=raw("SELECT 'CAT='||logical_schema||'|'||logical_table||'|'||protocol_version::text FROM public.sse_v1_catalog;")
    if create:need(local[1].split('\t')==[parts[0],oid,'public','week_demo','1'] and 'CAT=public|week_demo|1' in catalog,'initial local/server version mismatch')
    else:need(len(local)==1 and not any(x.startswith('CAT=') for x in catalog),'unexpected initial registration')
    initialization[db]=dict(database_oid=oid,key_metadata_valid=True,key_id_matches_environment=True,deployment_database_instance_match=True,mapping_matches=True,local_server_version_matches=True,registered=create)
    files()

def keys_mapping():return {n:(state/n).read_bytes() for n in ('row_aead_v1.key','locator_aead_v1.key','row_aead_v1.keyid','sse_table.mat','row_aead_tables.v1','environment.v1')}
def seed(name,create=True):
    global db,state
    db='postgres';raw('CREATE DATABASE '+name);db=name
    p=subprocess.run([str(repo/'dest/bin/gsql')]+conn+['-d',db,'-f',str(repo/'sse_sql/SSE_v1.sql')],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    need(p.returncode==0,p.stderr.decode());state=run/'evidence'/name
    info='host=%s port=%s dbname=%s user=omm'%(run/'socket',os.environ['ISOLATED_PORT'],db)
    p=subprocess.run([str(build/'initialize-live'),info,str(state)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    need(p.returncode==0,p.stderr.decode())
    initialization[db]=dict(initializer_command=[str(build/'initialize-live'),info,str(state)],initializer_exit_code=p.returncode,initializer_stdout=p.stdout.decode(errors='replace'))
    if create:
        good("CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text) WITH (sse_storage='ROW_AEAD_V1');")
        good("INSERT INTO week_demo VALUES ('1','apple','red apple'),('2','apple','green apple');")
    initial=initialization[db];initial_consistency(create);initialization[db].update(initial)
def business_lines(stdout):
    return [line for line in stdout.splitlines() if line not in STARTUP_LINES]


def rows():
    stdout, trace = good("SELECT * FROM week_demo WHERE keyword='apple';")
    need(sorted(business_lines(stdout)) == APPLE_ROWS, 'wrong row multiset')
    need('STATE_EVENT fides_setup' in trace, 'setup missing')


def fail_result(rc, stdout, stderr, status):
    need(rc != 0 and status in stderr and not business_lines(stdout),
         'incorrect failure or business output: ' + stderr)


def blocked():
    before = files()
    result = query("SELECT * FROM week_demo WHERE keyword='apple';")
    fail_result(*result, 'incomplete_operation')
    need(files() == before, 'blocked reopen changed counters, keys, mapping or marker')
expectations={
 '04-01':'Fresh initialization matches keys, scope, mapping and server version; new clients and real server restart/setup return ind 1/2 once; counters 2/4/6/8/10.',
 '04-02':'Marker rename EIO rejects SELECT and INSERT before count/index mutation; state and database unchanged.',
 '04-03':'Counter rename EIO after COMMIT preserves old local count and operation marker; fresh client is blocked.',
 '04-04':'SIGKILL at counter_advanced rolls back database transaction; marker survives and fresh client is blocked.',
 '04-05':'Real lock probe rejects competition; second gsql is blocked without effects; first can finish after release.',
 '04-06':'Missing row key rejects two fresh clients without regeneration or database effects.',
 '04-07':'Truncated counter rejects two fresh clients without replacing corrupt state.',
 '04-08':'Explicit new table registration is recognized by a new client; unknown word is healthy empty result.',
 '04-09':'Physical CREATE error rolls back server objects/registration, leaves marker and rejects fresh client.',
 '04-10':'Mapping save failure leaves partial registration plus marker; fresh client is blocked, no legacy fallback.',
 '04-11':'Current reinsert error suppresses display, rolls back index 2 to 2 and blocks fresh client.',
 '04-12':'Legacy nontransaction control reproduces index 2 to 0; marker remains and fresh client is blocked.',
 '04-13':'Real mode 0500 after COMMIT denies file creation as omm; count save fails with EACCES; marker persists; fresh client remains blocked.'}
def case(name,fn):
    first=seq;raw_first=len(raw_records)
    try:
        data=fn() or {};results.append(dict(case=name,result='PASS',database=db,state_dir=str(state),requests=list(range(first+1,seq+1)),commit=commit,expected=expectations[name[:5]],initialization=initialization.get(db,{}),final_state=metadata(),commands=name+'.commands.json',private_snapshots=str(private),**data));print('PASS '+name,flush=True)
    except Exception as e:
        results.append(dict(case=name,result='FAIL',reason=str(e),commit=commit));raise
    finally:
        for number in range(first+1,seq+1):
            proc,record=request_records[number];record['exit_code']=proc.poll()
            record['events']=[line for line in (out/record['stderr']).read_text().splitlines() if line.startswith('STATE_EVENT ')]
            (out/('request-%03d.command.json'%number)).write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
        (out/(name+'.commands.json')).write_text(json.dumps(raw_records[raw_first:],ensure_ascii=False,indent=2)+'\n',encoding='utf8')
        verdict = 'INCOMPLETE'
        if any(item['result'] == 'FAIL' for item in results):
            verdict = 'FAIL'
        elif len(results) == EXPECTED_CASES:
            verdict = 'PASS'
        summary = {'commit': commit, 'cases': results, 'result': verdict}
        (out / 'results.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
def normal():
    seed('s04_normal');stable=keys_mapping();fides_keys={str(p):p.read_bytes() for p in list((run/'client').glob('*.key'))+[run/'server/tdp_pk.key']};history=[{'stage':'insert','counter':count(),'rows':2}]
    for stage in ('first_query','repeat_query','client_reopen'):
        rows();history.append(dict(stage=stage,counter=count(),rows=2))
    before=files();pid=(run/'db/instance/postmaster.pid').read_text().splitlines()[0]
    a=subprocess.run([str(repo/'dest/bin/gs_ctl'),'stop','-D',str(run/'db/instance'),'-m','fast','-w'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    need(a.returncode==0,'server stop failed')
    a=subprocess.run([str(repo/'dest/bin/gs_ctl'),'start','-D',str(run/'db/instance'),'-Z','single_node','-w','-l',str(run/'private/restart.log'),'-o','-p %s -c listen_addresses= -k %s'%(os.environ['ISOLATED_PORT'],run/'socket')],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    need(a.returncode==0,'server start failed: '+a.stderr.decode());time.sleep(2)
    need(files()==before,'state changed merely by restarting server');need(pid!=(run/'db/instance/postmaster.pid').read_text().splitlines()[0],'server PID did not change')
    rows();history.append(dict(stage='server_restart_setup_query',counter=count(),rows=2));need(keys_mapping()==stable,'keys/mapping changed');need(all(Path(p).read_bytes()==v for p,v in fides_keys.items()),'Fides base keys changed')
    need([step['counter'] for step in history] == [2, 4, 6, 8, 10], 'wrong Fides counter progression')
    need(database_snapshot().startswith('SNAP=2:2:'), 'repeated queries changed table counts')
    return dict(history=history,state_unchanged_during_restart=True,key_mapping_unchanged=True,fides_base_keys_unchanged=True)
def marker():
    seed('s04_marker');before=files();snap=database_snapshot()
    for sql in ("SELECT * FROM week_demo WHERE keyword='apple';","INSERT INTO week_demo VALUES ('3','apple','not-written');"):
        rc,s,t=query(sql,'marker');fail_result(rc,s,t,'state_invalid');need('STATE_EVENT counter_advanced' not in t and 'STATE_EVENT search_take_sent' not in t,'effect before marker')
        need(files()==before and database_snapshot()==snap,'marker error modified state/db')
    return dict(state_database_unchanged=True)
def counter():
    seed('s04_counter');before=(state/'fides_client.mat').read_bytes();snap=database_snapshot()
    rc,s,t=query("SELECT * FROM week_demo WHERE keyword='apple';",'counter');fail_result(rc,s,t,'state_invalid')
    need('STATE_EVENT search_commit' in t and 'STATE_EVENT complete_durable' not in t,'wrong save failure phase')
    need((state/'fides_client.mat').read_bytes()==before and database_snapshot()!=snap,'expected committed index with old counters')
    blocked();return dict(marker_retained=True,local_count=2,database_committed=True)
def killed():
    seed('s04_kill');snap=database_snapshot();rc,s,t=query("SELECT * FROM week_demo WHERE keyword='apple';",'kill')
    need(rc == -9 and 'STATE_EVENT counter_advanced' in t and not business_lines(s),
         'process not killed at real phase or displayed business rows')
    for _ in range(30):
        if database_snapshot()==snap:break
        time.sleep(.1)
    need(database_snapshot()==snap,'server did not roll back disconnected search');blocked()
    return dict(signal='SIGKILL',database_rollback=True,marker_retained=True)
def compete():
    seed('s04_compete');signals=out/'signals';signals.mkdir(mode=0o700);snap=database_snapshot()
    p,name=query("SELECT * FROM week_demo WHERE keyword='apple';",'pause',True)
    try:
        for _ in range(100):
            if (signals/'ready').exists():break
            need(p.poll() is None,'first client exited before pause');time.sleep(.1)
        need((signals/'ready').exists(),'pause not reached')
        with (state/'operation.lock').open('r+') as f:
            try:fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:pass
            else:raise RuntimeError('actual directory lock not held')
        before=files();blocked();need(files()==before and database_snapshot()==snap,'second client had effects')
    finally:
        (signals/'release').touch();p.wait(timeout=90)
    need(p.returncode==0 and count()==4 and not (state/'operation.v1').exists(),'first client did not complete')
    return dict(lock_competition='EWOULDBLOCK',second_client='incomplete_operation',first_client_completed=True)
def missing():
    seed('s04_missing');(state/'row_aead_v1.key').rename(state/'row_aead_v1.key.held');before=files();snap=database_snapshot()
    rc,s,t=query("SELECT * FROM week_demo WHERE keyword='apple';");fail_result(rc,s,t,'key_invalid');need(files()==before and database_snapshot()==snap,'missing key recreated or effects')
    rc,s,t=query("SELECT * FROM week_demo WHERE keyword='apple';");fail_result(rc,s,t,'key_invalid');need(files()==before and database_snapshot()==snap,'second missing-key client changed files/db')
    return dict(no_key_recreation=True,reopen_blocked=True)
def broken():
    seed('s04_broken');(state/'fides_client.mat').write_text('apple 2');before=files()
    rc,s,t=query("SELECT * FROM week_demo WHERE keyword='apple';");fail_result(rc,s,t,'state_invalid');need(files()==before,'corruption silently replaced')
    rc,s,t=query("SELECT * FROM week_demo WHERE keyword='apple';");fail_result(rc,s,t,'state_invalid');need(files()==before,'reopen silently replaced corrupt state')
    return dict(reopen_blocked=True)
def create_reopen():
    seed('s04_create',False);good("CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text) WITH (sse_storage='ROW_AEAD_V1');");before=files()
    s, t = good("SELECT * FROM week_demo WHERE keyword='unknown';")
    need(files() == before and 'unknown_empty' in t and not business_lines(s), 'table version reopen failed')
def physical():
    seed('s04_physical',False)
    raw('ALTER FUNCTION public.sse_v1_create(text,text) RENAME TO original_create;')
    raw("CREATE FUNCTION public.sse_v1_create(text,text) RETURNS void LANGUAGE plpgsql AS $$ BEGIN PERFORM public.original_create($1,$2); RAISE EXCEPTION 'injected physical create rollback'; END; $$;")
    rc,s,t=query("CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text) WITH (sse_storage='ROW_AEAD_V1');");fail_result(rc,s,t,'db_failed')
    a=raw("SELECT 'OBJECTS='||count(*) FROM pg_tables WHERE schemaname='public' AND tablename LIKE 'week_demo%';")
    need('OBJECTS=0' in a,'DDL rollback left physical tables');need('REG=0' in raw("SELECT 'REG='||count(*) FROM public.sse_v1_catalog;"),'failed DDL left server registration');need((state/'sse_table.mat').read_bytes()==b'','local mapping incorrectly published');blocked()
def mapping():
    seed('s04_mapping',False);rc,s,t=query("CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text) WITH (sse_storage='ROW_AEAD_V1');",'mapping');fail_result(rc,s,t,'state_invalid')
    need((state/'sse_table.mat').read_bytes()==b'' and (state/'operation.v1').exists(),'partial mapping not blocked');need('REG=1' in raw("SELECT 'REG='||count(*) FROM public.sse_v1_catalog;"),'expected confirmed server registration');need('LEGACY=0' in raw("SELECT 'LEGACY='||count(*) FROM pg_tables WHERE schemaname='public' AND tablename='week_demo';"),'legacy plaintext table created');blocked()
    return dict(server_created=True,partial_local_registry_preserved=True,not_usable=True)
def reinsert(legacy=False):
    seed('s04_legacy' if legacy else 's04_reinsert');snap=database_snapshot()
    raw("CREATE FUNCTION fail_insert() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'injected reinsert failure'; END; $$;")
    raw('CREATE TRIGGER reject_insert BEFORE INSERT ON week_demo_index FOR EACH ROW EXECUTE PROCEDURE fail_insert();')
    if legacy:
        info='host=%s port=%s dbname=%s user=omm'%(run/'socket',os.environ['ISOLATED_PORT'],db)
        env=dict(os.environ,SSE_V1_STATE_DIR=str(state))
        p=subprocess.run([str(build/'legacy-control'),info,str(state)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        (out/'legacy-control.log').write_bytes(p.stdout+p.stderr)
        need(p.returncode==0 and 'LEGACY_INDEX_AFTER=0' in p.stdout.decode(),'legacy control failed: '+p.stderr.decode())
        need(database_snapshot().startswith('SNAP=2:0:'),'legacy index not zero');blocked()
        return dict(index_before=2,index_after=0,control_only=True,marker_retained=True)
    rc,s,t=query("SELECT * FROM week_demo WHERE keyword='apple';");fail_result(rc,s,t,'db_failed')
    need('STATE_EVENT reinsert_sent' in t and 'injected reinsert failure' in t,
         'failure did not reach the reinsert trigger')
    need(database_snapshot() == snap and 'STATE_EVENT fetch_sent' not in t,
         'current transaction did not roll back cleanly')
    blocked()
    return dict(index_before=2,index_after=2,current_transaction_rollback=True,marker_retained=True)
def readonly():
    seed('s04_readonly');before=(state/'fides_client.mat').read_bytes();snap=database_snapshot()
    rc,s,t=query("SELECT * FROM week_demo WHERE keyword='apple';",'readonly');fail_result(rc,s,t,'state_invalid')
    need('STATE_EVENT search_commit' in t and 'Permission denied' in t,'not a real post-commit permission failure')
    need(stat.S_IMODE(state.stat().st_mode)==0o500 and os.geteuid()!=0,'directory not readonly for actual omm client')
    try:
        fd=os.open(str(state/'write-probe'),os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
    except OSError as e:need(e.errno==errno.EACCES,'wrong OS permission failure')
    else:
        os.close(fd);raise RuntimeError('directory unexpectedly writable')
    need((state/'fides_client.mat').read_bytes()==before and database_snapshot()!=snap and (state/'operation.v1').exists(),'permission failure not preserved')
    frozen=files();rc,s,t=query("SELECT * FROM week_demo WHERE keyword='apple';");fail_result(rc,s,t,'state_invalid')
    need('STATE_EVENT search_take_sent' not in t and files()==frozen,'readonly reopen had side effects')
    return dict(real_permissions='0500',os_write_errno='EACCES',local_count=2,database_committed=True,marker_retained=True,reopen_status='state_invalid',no_permission_reset=True)

def reject_corrupt(kind):
    seed('s04_'+kind.replace('-','_'))
    if kind=='missing-locator':(state/'locator_aead_v1.key').rename(state/'locator_aead_v1.key.held')
    elif kind=='missing-keyid':(state/'row_aead_v1.keyid').rename(state/'row_aead_v1.keyid.held')
    elif kind=='short-row':(state/'row_aead_v1.key').write_bytes(b'short')
    elif kind=='short-locator':(state/'locator_aead_v1.key').write_bytes(b'short')
    elif kind=='wrong-keyid':
        f=state/'row_aead_v1.keyid';v=bytearray(f.read_bytes());v[0]^=1;f.write_bytes(v)
    elif kind=='key-permissions':(state/'row_aead_v1.key').chmod(0o644)
    elif kind=='bad-mapping':(state/'sse_table.mat').write_text('invalid mapping\n')
    elif kind=='missing-counter':(state/'fides_client.mat').rename(state/'fides_client.mat.held')
    elif kind=='missing-mapping':(state/'sse_table.mat').rename(state/'sse_table.mat.held')
    elif kind=='server-version':
        raw('ALTER TABLE public.sse_v1_catalog DROP CONSTRAINT sse_v1_catalog_protocol_version_check;')
        raw('UPDATE public.sse_v1_catalog SET protocol_version=2;')
    elif kind=='physical-shape':raw('ALTER TABLE public.week_demo_data ADD COLUMN unexpected text;')
    else:raise RuntimeError(kind)
    before=files();snap=database_snapshot();status='key_invalid' if kind in ('missing-locator','missing-keyid','short-row','short-locator','wrong-keyid','key-permissions') else 'state_invalid'
    for sql in ("SELECT * FROM week_demo WHERE keyword='apple';","INSERT INTO week_demo VALUES ('3','apple','not-written');"):
        rc,o,t=query(sql);fail_result(rc,o,t,status)
        need(not any('STATE_EVENT '+phase in t for phase in ('begin_durable','counter_advanced','insert_sent','search_take_sent')),'corrupt state reached side effect')
        need(files()==before and database_snapshot()==snap,'corrupt input recreated or request changed data')
    return dict(status=status,reopen_blocked=True,no_file_recreation=True,state_database_unchanged=True)

def empty_token(mode):
    seed('s04_'+mode.replace('-','_'));before=(state/'fides_client.mat').read_bytes();snap=database_snapshot()
    rc,o,t=query("SELECT * FROM week_demo WHERE keyword='apple';",mode);fail_result(rc,o,t,'protocol_invalid')
    need('STATE_EVENT begin_durable' in t and 'STATE_EVENT search_take_sent' not in t and 'STATE_EVENT counter_advanced' not in t,'empty token sent or changed count')
    need((state/'fides_client.mat').read_bytes()==before and database_snapshot()==snap,'empty token changed data');blocked()
    return dict(status='protocol_invalid',local_count=2,marker_retained=True,state_database_unchanged=True)

def persistence(mode):
    seed('s04_'+mode.replace('-','_'));before=files();snap=database_snapshot()
    rc,o,t=query("SELECT * FROM week_demo WHERE keyword='apple';",mode);fail_result(rc,o,t,'state_invalid')
    need('FAULT_INJECTED scoped-state-io' in t,'fault did not reach targeted syscall')
    marker=mode=='marker-dir-fsync' or not mode.startswith('marker-')
    if mode.startswith('marker-'):
        need('STATE_EVENT counter_advanced' not in t and 'STATE_EVENT search_take_sent' not in t and database_snapshot()==snap,'effect before marker durability')
        if not marker:need(files()==before,'failed unpublished marker altered state')
    else:
        need('STATE_EVENT search_commit' in t and 'STATE_EVENT complete_durable' not in t,'wrong persistence stage')
        expected=4 if mode in ('counter-dir-fsync','clear-unlink','clear-dir-fsync') else 2
        need(count()==expected and database_snapshot()!=snap,'wrong committed/save outcome')
    need((state/'operation.v1').exists()==marker,'incorrect retained marker')
    if marker:blocked()
    else:
        rows();need(count()==4 and not (state/'operation.v1').exists(),'clean retry after pre-effect failure not possible')
    return dict(fault=mode,status='state_invalid',marker_retained=marker,syscall_fault_observed=True,final_counter=count())

case('04-01-normal-restart-and-counters',normal)
case('04-02-marker-before-effects',marker)
case('04-03-counter-save-failure',counter)
case('04-04-real-process-kill',killed)
case('04-05-two-clients',compete)
case('04-06-existing-data-missing-key',missing)
case('04-07-broken-state',broken)
case('04-08-create-and-reopen',create_reopen)
case('04-09-physical-create-failure',physical)
case('04-10-mapping-save-failure',mapping)
case('04-11-current-reinsert-rollback',reinsert)
case('04-12-legacy-two-to-zero',lambda:reinsert(True))
case('04-13-real-state-unwritable',readonly)
next_id=14
for kind in ('missing-locator','missing-keyid','short-row','short-locator','wrong-keyid','key-permissions','bad-mapping','missing-counter','missing-mapping','server-version','physical-shape'):
    name='04-%02d-%s'%(next_id,kind);expectations[name[:5]]='Two fresh SELECT/INSERT clients reject invalid existing state before effects; no regeneration, state and database unchanged.'
    case(name,lambda kind=kind:reject_corrupt(kind));next_id+=1
for mode in ('empty-token','empty-derivation'):
    name='04-%02d-%s'%(next_id,mode);expectations[name[:5]]='Registered empty token component is protocol_invalid, not healthy unknown; no counter/index change; marker and restart block retained.'
    case(name,lambda mode=mode:empty_token(mode));next_id+=1
for mode in ('marker-write','marker-fsync','marker-close','marker-dir-fsync','counter-write','counter-fsync','counter-close','counter-dir-fsync','clear-unlink','clear-dir-fsync'):
    name='04-%02d-%s'%(next_id,mode);expectations[name[:5]]='Scoped syscall failure is observed and returned; no pre-marker effects; post-change marker survives fresh-client rejection; unpublished marker failure leaves clean state.'
    case(name,lambda mode=mode:persistence(mode));next_id+=1
need(len(results) == EXPECTED_CASES, 'incomplete LZM-04 case matrix')
print('LZM04_CASES=%d RESULT=PASS' % len(results), flush=True)
