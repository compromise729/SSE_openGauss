import argparse, subprocess, sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--repo',required=True);p.add_argument('--out',required=True);a=p.parse_args()
r=Path(a.repo).resolve();o=Path(a.out).resolve();o.mkdir(exist_ok=True)
g=o/'generated/storage';g.mkdir(parents=True,exist_ok=True)
subprocess.check_call(['perl',str(r/'src/gausskernel/storage/lmgr/generate-lwlocknames.pl'),str(r/'src/gausskernel/storage/lmgr/lwlocknames.txt')],cwd=str(g))
u=o/'generated/utils';u.mkdir(exist_ok=True)
with (u/'probes.h').open('w') as f: subprocess.check_call(['sed','-f',str(r/'src/common/backend/utils/Gen_dummy_probes.sed'),str(r/'src/common/backend/utils/probes.d')],stdout=f)
subprocess.check_call(['perl',str(r/'src/common/backend/utils/Gen_fmgrtab.pl'),str(r/'src/common/backend/catalog/builtin_funcs.ini')],cwd=str(u))
with (u/'errcodes.h').open('w') as f: subprocess.check_call(['perl',str(r/'src/common/backend/utils/generate-errcodes.pl'),str(r/'src/common/backend/utils/errcodes.txt')],stdout=f)
t=o/'generated/gstrace';t.mkdir(exist_ok=True)
subprocess.check_call(['/usr/bin/python',str(r/'src/lib/gstrace/script/trc_gen.py'),'-s',str(r/'src/lib/gstrace/config')+'/', '-o',str(t)])
s=(r/'team_week/liu_zhuoming/LZM-03/preflight/build-live.py').read_text()
needle='command=shlex.split(line);command[command.index("-o")+1]=str(out/(name+".o"))'
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
