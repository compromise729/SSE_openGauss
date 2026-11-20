"""Read-only local evidence; never repair keys, counters or markers."""
import argparse,json,os,stat,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--state',required=True);a=p.parse_args();root=Path(a.state)
out={'state_dir':str(root.resolve()),'operation_incomplete':(root/'operation.v1').exists(),'files':{}}
keys={'row_aead_v1.key','locator_aead_v1.key','row_aead_v1.keyid'}
for name in sorted(keys|{'operation.v1','operation.lock','deployment.v1','environment.v1','sse_table.mat','row_aead_tables.v1','fides_client.mat'}):
    file=root/name
    try:
        s=file.lstat();entry={'present':True,'regular':stat.S_ISREG(s.st_mode),'size':s.st_size,'mode':oct(stat.S_IMODE(s.st_mode)),'uid':s.st_uid}
        if name not in keys and stat.S_ISREG(s.st_mode) and name!='deployment.v1':entry['sha256']=hashlib.sha256(file.read_bytes()).hexdigest()
        out['files'][name]=entry
    except OSError as e:out['files'][name]={'present':False,'error':e.strerror}
print(json.dumps(out,ensure_ascii=False,indent=2))
