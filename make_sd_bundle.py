#!/usr/bin/env python3
"""Generate the strict v1 manifest for an explicitly reviewed SD bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import re

def generate(root, name, firmware='any'):
    root=Path(root)
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,48}',name):
        raise ValueError('Name must be 1..48 ASCII letters, digits, dot, underscore or hyphen')
    if firmware!='any' and not re.fullmatch(r'[a-f0-9]{64}',firmware):
        raise ValueError('Firmware must be the original JXE SHA256 or any')
    files=[]
    for p in sorted(root.rglob('*')):
        if p.is_symlink():raise ValueError('Symlinks are not allowed')
        if not p.is_file() or p.name=='manifest.json' and p.parent==root:continue
        rel=p.relative_to(root).as_posix()
        if len(rel)>=96 or not re.fullmatch(r'[A-Za-z0-9._/-]+',rel):raise ValueError('Invalid path: '+rel)
        data=p.read_bytes()
        if len(data)>1024*1024:raise ValueError('File exceeds 1 MiB: '+rel)
        files.append(dict(path=rel,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
    if not {'start.sh','stop.sh'} <= {f['path'] for f in files}:raise ValueError('start.sh and stop.sh required')
    if len(files)>32 or sum(f['size'] for f in files)>8*1024*1024:raise ValueError('Bundle exceeds bounds')
    manifest=dict(format=1,name=name,firmware_sha256=firmware,files=files)
    data=(json.dumps(manifest,indent=2)+'\n').encode()
    if len(data)>16384:raise ValueError('Manifest too large')
    (root/'manifest.json').write_bytes(data)
    return hashlib.sha256(data).hexdigest()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path);p.add_argument('--name',required=True)
    p.add_argument('--firmware',default='any',help='Use any only for reviewed firmware-independent scripts')
    a=p.parse_args();print(generate(a.directory,a.name,a.firmware))
