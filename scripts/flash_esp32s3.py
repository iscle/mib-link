#!/usr/bin/env python3
"""Flash the three ESP32-S3 image regions without erasing the NVS settings area."""
import argparse
from importlib.metadata import version,PackageNotFoundError
import hashlib
import json
from pathlib import Path
import subprocess
import sys

def command(root,port,baud,esptool_major=5):
    manifest=json.loads((root/'flash.json').read_text())
    if manifest['chip']!='esp32s3':raise ValueError('Wrong chip in flash manifest')
    arguments=[sys.executable,'-m','esptool','--chip','esp32s3','--port',port,'--baud',str(baud),'write-flash' if esptool_major>=5 else 'write_flash']
    expected={0x0:'bootloader.bin',0x8000:'partition-table.bin',0x10000:'mst-link-esp32s3.bin'}
    seen=set()
    for image in manifest['images']:
        offset=int(image['offset'],0);name=image['file']
        if expected.get(offset)!=name or offset in seen:raise ValueError('Unexpected flash region')
        seen.add(offset);data=(root/name).read_bytes()
        limit={0:0x8000,0x8000:0x1000,0x10000:0x300000}[offset]
        if not data or len(data)>limit or hashlib.sha256(data).hexdigest()!=image['sha256']:raise ValueError('Invalid image: '+name)
        arguments += [hex(offset),str(root/name)]
    if seen!=set(expected):raise ValueError('Incomplete flash bundle')
    return arguments

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--baud',type=int,default=460800)
    args=parser.parse_args()
    try: major=int(version("esptool").split(".")[0])
    except PackageNotFoundError: raise SystemExit("Install esptool first: python3 -m pip install 'esptool>=4.12,<6'")
    if major not in (4,5):raise SystemExit("Use esptool 4.12 or 5.x")
    subprocess.run(command(Path(__file__).resolve().parent,args.port,args.baud,major),check=True)
if __name__=='__main__':main()
