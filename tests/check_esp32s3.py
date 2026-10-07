#!/usr/bin/env python3
"""Validate the actual ESP32-S3 images, partition table and settings-preserving flash bundle."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import zipfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from scripts.flash_esp32s3 import command

def image(data):
    assert len(data)>24 and data[0]==0xe9 and 1<=data[1]<=16
    assert struct.unpack_from('<H',data,12)[0]==9,'Image is not for ESP32-S3'
    assert data[2]==2 and data[3]==0x20,'Expected DIO, 4 MB, 40 MHz'
    offset=24;check=0xef
    for _ in range(data[1]):
        address,size=struct.unpack_from('<II',data,offset);offset+=8
        assert offset+size<=len(data)
        for b in data[offset:offset+size]:check^=b
        offset+=size
    checksum_at=(offset//16)*16+15
    assert checksum_at<len(data) and data[checksum_at]==check,'ESP segment checksum mismatch'
    if data[23]:
        end=checksum_at+1
        assert data[end:end+32]==hashlib.sha256(data[:end]).digest(),'ESP appended hash mismatch'
        assert end+32==len(data)

archive=ROOT/'dist/mst-link-esp32s3.zip'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==(ROOT/'dist/ESP32S3-SHA256SUMS').read_text().split()[0]
with tempfile.TemporaryDirectory() as temp:
    root=Path(temp)
    with zipfile.ZipFile(archive) as z:
        assert set(z.namelist())=={'bootloader.bin','partition-table.bin','mst-link-esp32s3.bin','flash.json','flash.py','README.txt'}
        z.extractall(root)
    manifest=json.loads((root/'flash.json').read_text());assert manifest['chip']=='esp32s3'
    for name in ['bootloader.bin','mst-link-esp32s3.bin']:image((root/name).read_bytes())
    table=(root/'partition-table.bin').read_bytes();partitions={};offset=0
    while table[offset:offset+2]==b'\xaa\x50':
        magic,kind,subtype,start,size,label,flags=struct.unpack_from('<HBBII16sI',table,offset)
        partitions[label.rstrip(b'\0').decode()]=(kind,subtype,start,size);offset+=32
    assert table[offset:offset+2]==b'\xeb\xeb'
    assert table[offset+16:offset+32]==hashlib.md5(table[:offset]).digest()
    assert partitions['nvs']==(1,2,0x9000,0x6000)
    assert partitions['factory']==(0,0,0x10000,0x300000)
    for major,verb in [(4,'write_flash'),(5,'write-flash')]:
        args=command(root,'TEST_PORT',460800,major);assert verb in args
        assert [args[i] for i in range(args.index(verb)+1,len(args),2)]==['0x0','0x8000','0x10000']
    # The helper must refuse corruption or an offset that could erase NVS.
    def rejected():
        try:command(root,'TEST_PORT',460800)
        except ValueError:return
        raise AssertionError('Unsafe flash bundle accepted')
    p=root/'mst-link-esp32s3.bin';original=p.read_bytes();p.write_bytes(b'bad'+original[3:]);rejected();p.write_bytes(original)
    manifest['images'][1]['offset']='0x9000';(root/'flash.json').write_text(json.dumps(manifest));rejected()
config=(ROOT/'build/esp32s3/config/sdkconfig.h').read_text()
for flag in ['CONFIG_LWIP_TCPIP_CORE_LOCKING 1','CONFIG_LWIP_CHECK_THREAD_SAFETY 1','CONFIG_ESP_CONSOLE_SECONDARY_NONE 1']:
    assert '#define '+flag in config
for flag in ['CONFIG_FREERTOS_UNICORE','CONFIG_SPIRAM','CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG','CONFIG_ESP_CONSOLE_USB_CDC']:
    assert '#define '+flag+' ' not in config,flag
print('PASS: ESP32-S3 image chip/checksum/hash, DIO/4 MB flash layout, NVS preservation, corrupt-bundle rejection and dual-core/thread-safe SDK configuration')
