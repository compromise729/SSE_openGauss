#!/usr/bin/env python3
"""Build isolated verification artifacts from existing compiler flags/objects."""
import argparse
import os
import shlex
import subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--repo',required=True)
p.add_argument('--out',required=True)
p.add_argument('--frontend-only',action='store_true')
args=p.parse_args()
repo=Path(args.repo).resolve();out=Path(args.out).resolve();out.mkdir(exist_ok=True)
os.chmod(str(out),0o711)
tools=repo/'team_week/liu_zhuoming/LZM-03/preflight'

def run(command,cwd,log):
    with (out/log).open('w') as f:
        rc=subprocess.call(command,cwd=str(cwd),stdout=f,stderr=subprocess.STDOUT)
    if rc:
        print((out/log).read_text()[-6000:]);raise SystemExit(rc)
    print('PASS BUILD '+log,flush=True)

def dry(directory,targets):
    proc=subprocess.run(['make','-n']+targets,cwd=str(directory),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if proc.returncode:raise RuntimeError(proc.stdout.decode(errors='replace')[-3000:])
    return proc.stdout.decode(errors='replace').splitlines()

def link_recipe(lines, target, drivers):
    for line in reversed(lines):
        if not line.rstrip().endswith(' -o ' + target):
            continue
        command = shlex.split(line)
        if Path(command[0]).name in drivers and command[-2:] == ['-o', target]:
            return line
    raise RuntimeError('make -n did not emit a compiler link command for ' + target)


def compile_object(directory,name):
    lines=dry(directory,['-W',name+'.cpp',name+'.o'])
    line=next(x for x in lines if '-c -o '+name+'.o '+name+'.cpp' in x)
    command=shlex.split(line);command[command.index('-o')+1]=str(out/(name+'.o'))
    run(command,directory,name+'.build.log')

# No make install and no in-place object replacement. Existing object files
# supply unchanged code; only the affected translation units are recompiled.
server=repo/'src/gausskernel/process/tcop'
if not args.frontend_only:
    for name in ('postgres','Server_Sophos'):compile_object(server,name)
    lines=dry(repo/'src/gausskernel',['gaussdb'])
    line=link_recipe(lines, 'gaussdb', ('g++',))
    command=shlex.split(line)
    for name in ('postgres','Server_Sophos'):
        old='../../src/gausskernel/process/tcop/'+name+'.o'
        command[command.index(old)]=str(out/(name+'.o'))
    command[command.index('-o')+1]=str(out/'gaussdb')
    run(command,repo/'src/gausskernel','gaussdb.link.log')
elif not (out/'gaussdb').is_file():
    raise RuntimeError('--frontend-only requires an existing temporary server')

include=Path(os.environ['BINARYLIBS'])/'dependency/centos7.6_x86_64/openssl/comm/include'
flags=['-std=c++14','-D_GLIBCXX_USE_CXX11_ABI=0','-pthread','-fPIC',
       '-I'+str(include),'-I'+str(repo/'src/include'),'-I'+str(repo/'src/include/libpq'),
       '-I'+str(repo/'sse/opensse-schemes/lib/include'),
       '-I'+str(repo/'sse/opensse-schemes/third_party/crypto/src/include'),
       '-I'+str(repo/'team_week/yan_zihan/YZH-01/include')]
libs=['-L'+str(repo/'src/common/interfaces/libpq'),'-Wl,-rpath,'+str(repo/'src/common/interfaces/libpq'),
      '-L'+str(repo/'dest/lib'),'-Wl,-rpath,'+str(repo/'dest/lib'),
      '-lpq_ce','-lsse_crypto','-lmitra_sse_crypto','-lsodium','-lgmp','-lcrypto']
client_source=repo/'src/common/interfaces/libpq/Client_Sophos.cpp'
client_copy=out/'Client_Sophos.verify.cpp'
text=client_source.read_text()
# The existing libpq already owns this nontrivial global. A test-only overlay
# must not construct/destruct it twice. Normal native builds have one owner.
definition='std::string storage_dir_path  = "/opt/og/openGauss-server/sse_storage";'
if text.count(definition)!=1:raise RuntimeError('review Client_Sophos global ownership')
client_copy.write_text(text.replace(definition,'extern std::string storage_dir_path;',1))
flags+=['-iquote',str(client_source.parent)]
sources=[repo/'src/common/interfaces/libpq'/name for name in
         ('row_state_v1.cpp','row_state_catalog.cpp','row_request_v1.cpp')]
sources.append(client_copy)
sources.append(repo/'team_week/yan_zihan/YZH-01/src/row_aead_v1.cpp')
run([os.environ.get('CXX','g++')]+flags+['-shared','-Wl,-z,defs','-Wl,-soname,librow_request_verify.so']+
    [str(x) for x in sources]+libs+['-o',str(out/'librow_request_verify.so')],repo,'module.build.log')
run([os.environ.get('CXX','g++')]+flags+['-shared',str(tools/'fault-hook.cpp'),
    str(tools/'live-fault-hook.cpp'),'-ldl','-o',str(out/'live-fault-hook.so')],repo,'hook.build.log')
run([os.environ.get('CXX','g++')]+flags+[str(tools/'initialize-live.cpp'),
    '-L'+str(out),'-Wl,-rpath,'+str(out),'-lrow_request_verify']+libs+['-o',str(out/'initialize-live')],repo,'initialize.build.log')

client=repo/'src/bin/psql'
for name in ('startup','common'):compile_object(client,name)
lines=dry(client,['gsql'])
line=link_recipe(lines, 'gsql', ('g++', 'gcc'))
command=shlex.split(line)
for name in ('startup','common'):command[command.index(name+'.o')]=str(out/(name+'.o'))
command[1:1]=['-L'+str(out),'-Wl,-rpath,'+str(out),'-lrow_request_verify']
command[command.index('-o')+1]=str(out/'gsql')
run(command,client,'gsql.link.log')
for name in ('gaussdb','gsql','initialize-live','librow_request_verify.so','live-fault-hook.so'):
    os.chmod(str(out/name),0o755)
print('LIVE_BUILD_DIR='+str(out))
