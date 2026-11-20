import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


# The tested source is frozen independently of the commit carrying these tools.
SOURCE_BASELINE = '5c8b2b03b482e203ac65a6f989a1b669126f263c'
SOURCE_PATHS = (
    'configure', 'GNUmakefile.in', 'src', 'sse', 'sse_sql', 'contrib',
    'team_week/yan_zihan/YZH-01/include', 'team_week/yan_zihan/YZH-01/src',
)
CONFIGURED_FILES = {
    'src/Makefile.global', 'src/include/pg_config.h',
    'src/bin/gsqlerr/errmsg.h', 'src/bin/gsqlerr/errmsg.log',
    'src/bin/gsqlerr/errmsg_new.txt', 'src/bin/gsqlerr/errmsg_result.txt',
    'src/bin/gsqlerr/gsqlerr_errmsg.h', 'src/gausskernel/storage/smgr/errmsg_bak.h',
}
INSTALLED_INPUTS = (
    'dest/bin/gsql', 'dest/bin/gaussdb', 'dest/lib/libpq_ce.so.5.5',
    'src/common/interfaces/libpq/libpq_ce.so.5.5', 'sse_sql/SSE_v1.sql',
    'team_week/liu_zhuoming/LZM-03/preflight/build-live.py',
    'team_week/liu_zhuoming/LZM-04/preflight/build-current.py',
)
TEST_PROGRAMS = ('gaussdb', 'gsql', 'initialize-live', 'librow_request_verify.so')


def git(repo, *args):
    command = ['git'] + list(args)
    if os.geteuid() == 0:
        # Older Git refreshes its index even for a working-tree diff.
        command = ['runuser', '-u', 'omm', '--'] + command
    return subprocess.check_output(command, cwd=str(repo)).decode('utf-8')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def build_inputs(repo):
    head = git(repo, 'rev-parse', 'HEAD').strip()
    git(repo, 'merge-base', '--is-ancestor', SOURCE_BASELINE, head)
    committed = git(repo, 'diff', '--name-only', '-z', SOURCE_BASELINE, head, '--', *SOURCE_PATHS)
    if committed:
        raise SystemExit('Source differs from the SHY-04 baseline: ' + ', '.join(committed.split('\0')[:-1]))
    local = set(git(repo, 'diff', '--name-only', '-z', 'HEAD', '--', *SOURCE_PATHS).split('\0')[:-1])
    unexpected = local - CONFIGURED_FILES
    if unexpected:
        raise SystemExit('Uncommitted source changes: ' + ', '.join(sorted(unexpected)))
    return {
        'checkout_head': head,
        'source_baseline': SOURCE_BASELINE,
        'input_sha256': {name: sha256(repo / name) for name in sorted(set(INSTALLED_INPUTS) | local)},
    }


def build_outputs(out):
    return {name: sha256(out / name) for name in TEST_PROGRAMS}


parser = argparse.ArgumentParser()
parser.add_argument('--repo', required=True)
parser.add_argument('--out', required=True)
parser.add_argument('--verify-inputs', action='store_true')
args = parser.parse_args()
r, o = Path(args.repo).resolve(), Path(args.out).resolve()
manifest = o / 'build-input.json'
inputs = build_inputs(r)
if args.verify_inputs:
    if not manifest.is_file():
        raise SystemExit('Missing build-input.json. Run the LZM-04 build first.')
    expected = dict(inputs, artifact_sha256=build_outputs(o))
    if json.loads(manifest.read_text(encoding='utf-8')) != expected:
        raise SystemExit('Build inputs or test programs no longer match this checkout. Rebuild in a new directory.')
    print('LZM04_TESTED_HEAD={}'.format(inputs['checkout_head']))
    print('LZM04_SOURCE_BASELINE={}'.format(inputs['source_baseline']))
    raise SystemExit(0)
if manifest.exists():
    raise SystemExit('Use a new directory for this verification build.')
o.mkdir(exist_ok=True)
g=o/'generated/storage';g.mkdir(parents=True,exist_ok=True)
subprocess.check_call(['perl',str(r/'src/gausskernel/storage/lmgr/generate-lwlocknames.pl'),str(r/'src/gausskernel/storage/lmgr/lwlocknames.txt')],cwd=str(g))
u=o/'generated/utils';u.mkdir(exist_ok=True)
with (u/'probes.h').open('w') as f: subprocess.check_call(['sed','-f',str(r/'src/common/backend/utils/Gen_dummy_probes.sed'),str(r/'src/common/backend/utils/probes.d')],stdout=f)
subprocess.check_call(['perl',str(r/'src/common/backend/utils/Gen_fmgrtab.pl'),str(r/'src/common/backend/catalog/builtin_funcs.ini')],cwd=str(u))
with (u/'errcodes.h').open('w') as f: subprocess.check_call(['perl',str(r/'src/common/backend/utils/generate-errcodes.pl'),str(r/'src/common/backend/utils/errcodes.txt')],stdout=f)
t=o/'generated/gstrace';t.mkdir(exist_ok=True)
subprocess.check_call(['/usr/bin/python',str(r/'src/lib/gstrace/script/trc_gen.py'),'-s',str(r/'src/lib/gstrace/config')+'/', '-o',str(t)])
s=(r/'team_week/liu_zhuoming/LZM-03/preflight/build-live.py').read_text()
# Inject an additional include directory without changing tracked/generated source headers.
old="command=shlex.split(line);command[command.index('-o')+1]=str(out/(name+'.o'))"
assert s.count(old)==1
s=s.replace(old,old+";command.insert(1,'-I'+str(out/'generated'));command.insert(2,'-I'+str(repo/'src/include/gstrace'))")

# Dry make prints object-list recipes when stash removed generated objfiles.txt.
# Recover these lists in memory; never recreate them in the checkout.
old_link="command=shlex.split(line)\n    for name in ('postgres','Server_Sophos'):"
new_link="""command=shlex.split(line)
    import re
    objects=[]
    for recipe in lines:
        match=re.search(r'echo (.*?) \) >objfiles.txt',recipe)
        if match:
            for name in match.group(1).split():
                path=repo/name
                if name.endswith(('.o','.a')) and path.is_file() and not name.startswith('contrib/postgres_fdw/'):
                    value=str(path)
                    if value not in objects and not any(x.endswith(name) for x in command):objects.append(value)
    command[1:1]=objects
    for name in ('postgres','Server_Sophos'):"""
assert s.count(old_link)==1
s=s.replace(old_link,new_link)
# Object paths may now be absolute because they came from virtual lists.
s=s.replace("command[command.index(old)]=str(out/(name+'.o'))", "old=next(x for x in command if x.endswith('/process/tcop/'+name+'.o'));command[command.index(old)]=str(out/(name+'.o'))")

sys.argv=['build-live.py','--repo',str(r),'--out',str(o)]
exec(compile(s,'LZM03-build-with-private-generated-header','exec'),{'__name__':'__main__'})

if build_inputs(r) != inputs:
    raise SystemExit('Build inputs changed during verification build.')
manifest.write_text(json.dumps(dict(inputs, artifact_sha256=build_outputs(o)), indent=2) + '\n',
                    encoding='utf-8')
