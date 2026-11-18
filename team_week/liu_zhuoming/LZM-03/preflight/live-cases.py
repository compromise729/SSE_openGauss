#!/usr/bin/env python3
"""Exercise actual gsql -> state/crypto/Fides -> isolated SQL request paths."""
import json
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
    p,e=client("CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text);");succeeded(p)
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
    ordered(e,['preflight','begin_durable','token_generated','search_take_sent','counter_advanced','reinsert_sent','fetch_sent','rows_authenticated','state_saved','complete_durable'])
    require(count('apple')==before+2,'wrong real search/reinsert counter change')
    require(snapshot_server().startswith('SNAP=3:3:'),'search changed business/index row counts')
    require(keys_before=={p.name:p.read_bytes() for p in state.glob('*.key*')},'real keys changed across process reopen')
    require(not (state/'operation.v1').exists(),'search marker not cleared')
    return {'events':e,'counter_before':before,'counter_after':count('apple'),'rows':['1','2'],'key_bytes_unchanged':True}
def unknown():
    before=snapshot_files(state);server=snapshot_server()
    p,e=client("SELECT * FROM week_demo WHERE keyword='unknown';");succeeded(p)
    require('unknown_empty' in e,'unknown word not short-circuited');no_business_events(e)
    require(before==snapshot_files(state) and server==snapshot_server(),'unknown search changed state/database')
    require('red apple' not in p.stdout.decode(),'unknown search exposed rows')
    return {'events':e,'state_unchanged':True,'database_unchanged':True}
def marker_failure(insert_mode):
    before=snapshot_files(state);server=snapshot_server()
    sql="INSERT INTO week_demo VALUES ('4','apple','must-not-insert');" if insert_mode else "SELECT * FROM week_demo WHERE keyword='apple';"
    p,e=client(sql,fault='marker-rename')
    require(p.returncode!=0 and b'Fides state_invalid' in p.stderr and b'operation.v1' in p.stderr,'marker fault was not propagated')
    no_business_events(e)
    require(before==snapshot_files(state) and server==snapshot_server(),'marker failure followed by side effect')
    return {'events':e,'exit_code':p.returncode,'state_unchanged':True,'database_unchanged':True}
def broken():
    target=copy_state('broken-state');(target/'fides_client.mat').write_text('apple 6')
    before=snapshot_files(target);server=snapshot_server()
    p,e=client("SELECT * FROM week_demo WHERE keyword='apple';",target)
    require(p.returncode!=0 and b'Fides state_invalid' in p.stderr,'bad state not rejected')
    no_business_events(e);require(before==snapshot_files(target) and server==snapshot_server(),'bad state caused effects')
    return {'status':'state_invalid','events':e}
def missing(name):
    target=copy_state('missing-'+name)
    (target/name).rename(target/(name+'.held'))
    before=snapshot_files(target);server=snapshot_server()
    p,e=client("SELECT * FROM week_demo WHERE keyword='apple';",target)
    require(p.returncode!=0 and b'Fides key_invalid' in p.stderr,'missing existing key not rejected')
    no_business_events(e)
    require(not (target/name).exists() and before==snapshot_files(target) and server==snapshot_server(),'missing key recreated or effects occurred')
    return {'status':'key_invalid','events':e,'not_recreated':True}
def empty(fault):
    target=copy_state(fault);counter=(target/'fides_client.mat').read_bytes();server=snapshot_server()
    p,e=client("SELECT * FROM week_demo WHERE keyword='apple';",target,fault)
    require(p.returncode!=0 and b'Fides protocol_invalid' in p.stderr,'empty token not distinct protocol failure')
    ordered(e,['preflight','begin_durable','token_generated'])
    require('search_take_sent' not in e and 'counter_advanced' not in e,'bad token caused a side effect')
    require((target/'operation.v1').exists() and (target/'fides_client.mat').read_bytes()==counter,'bad token marker/counter mismatch')
    require(server==snapshot_server(),'bad token changed database')
    again,_=client("SELECT * FROM week_demo WHERE keyword='apple';",target)
    require(again.returncode!=0 and b'Fides incomplete_operation' in again.stderr,'reopen did not block bad token operation')
    require(b'red apple' not in p.stdout,'partial data displayed')
    return {'status':'protocol_invalid','events':e,'restart_status':'incomplete_operation','database_unchanged':True}
def reinsert_failure():
    target=copy_state('reinsert-failure')
    raw("CREATE FUNCTION public.fail_reinsert() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'injected reinsert failure'; END; $$;")
    raw("CREATE TRIGGER fail_reinsert BEFORE INSERT ON public.week_demo_index FOR EACH ROW EXECUTE PROCEDURE public.fail_reinsert();")
    p,e=client("SELECT * FROM week_demo WHERE keyword='apple';",target)
    require(p.returncode!=0 and b'Fides db_failed' in p.stderr,'real SQL reinsert failure not propagated')
    ordered(e,['begin_durable','search_take_sent','counter_advanced','reinsert_sent'])
    require('fetch_sent' not in e and 'complete_durable' not in e and b'red apple' not in p.stdout,'reinsert failure continued/displayed')
    require((target/'operation.v1').exists(),'real SQL failure lost marker')
    require(snapshot_server().startswith('SNAP=4:2:'),'expected two apple indexes removed, other two indexes retained')
    again,_=client("SELECT * FROM week_demo WHERE keyword='apple';",target)
    require(again.returncode!=0 and b'Fides incomplete_operation' in again.stderr,'real SQL failure not blocked on restart')
    return {'status':'db_failed','events':e,'data_rows':4,'remaining_index_rows':2,'restart_status':'incomplete_operation'}

def quoted_keyword():
    p,e=client("INSERT INTO week_demo VALUES ('4','red AND green','it''s 中文');");succeeded(p)
    ordered(e,['begin_durable','counter_advanced','insert_sent','state_saved','complete_durable'])
    for round_number in (1,2):
        p,e=client("SELECT * FROM week_demo WHERE keyword='red AND green';");succeeded(p)
        require("4|red AND green|it's 中文" in p.stdout.decode(),'quoted/UTF-8 row did not survive reopen')
        ordered(e,['begin_durable','search_take_sent','counter_advanced','complete_durable'])
        require(count('red AND green')==round_number+1,'space keyword counter snapshot not reloaded')
    return {'keyword':'red AND green','counter_after':3,'rounds':2,'utf8_and_sql_quote_preserved':True}

case('LIVE-01-create',create)
case('LIVE-02-insert',insert)
case('LIVE-03-search',search)
case('LIVE-04-reopen-search',search)
case('LIVE-05-unknown',unknown)
case('LIVE-06-marker-insert',lambda:marker_failure(True))
case('LIVE-07-marker-search',lambda:marker_failure(False))
case('LIVE-08-broken-state',broken)
for i,name in enumerate(('row_aead_v1.key','locator_aead_v1.key','row_aead_v1.keyid'),9):
    case('LIVE-%02d-missing-key'%i,lambda name=name:missing(name))
case('LIVE-12-empty-token',lambda:empty('empty-token'))
case('LIVE-13-empty-derivation',lambda:empty('empty-derivation'))
case('LIVE-14-quoted-keyword',quoted_keyword)
case('LIVE-15-reinsert-failure',reinsert_failure)
print('LIVE_RESULT=PASS',flush=True)
