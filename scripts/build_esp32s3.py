#!/usr/bin/env python3
"""Build and package ESP32-S3 using the activated, pinned ESP-IDF environment."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile
ROOT=Path(__file__).resolve().parents[1]
IDF_COMMIT='30aaf64524299d3bde422ca9a2848090d1bc5d0f'
TUSB_COMMIT='86ad6e56c1700e85f1c5678607a762cfe3aa2f47'

def build():
    idf=Path(os.environ.get('IDF_PATH',ROOT/'external/esp-idf')).resolve()
    for path,revision in [(idf,IDF_COMMIT),(ROOT/'external/tinyusb',TUSB_COMMIT)]:
        actual=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
        if actual!=revision:raise SystemExit('Unexpected SDK revision: '+str(path))
    if not os.environ.get('IDF_PYTHON_ENV_PATH'):raise SystemExit('Source ESP-IDF export.sh before building')
    from scripts.generate import generate
    generate()
    output=ROOT/'build/esp32s3'
    subprocess.run([sys.executable,str(idf/'tools/idf.py'),'-C',str(ROOT/'platform/esp32s3'),'-B',str(output),'build'],check=True)
    manifest={'chip':'esp32s3','version':'1.1.0','flash_size':'4MB','images':[]}
    bundle=ROOT/'dist/esp32s3';bundle.mkdir(parents=True,exist_ok=True)
    for offset,source,name in [('0x0','bootloader/bootloader.bin','bootloader.bin'),('0x8000','partition_table/partition-table.bin','partition-table.bin'),('0x10000','mst_link.bin','mst-link-esp32s3.bin')]:
        shutil.copy2(output/source,bundle/name)
        manifest['images'].append({'offset':offset,'file':name,'sha256':hashlib.sha256((bundle/name).read_bytes()).hexdigest()})
    (bundle/'flash.json').write_text(json.dumps(manifest,indent=2)+'\n')
    shutil.copy2(ROOT/'scripts/flash_esp32s3.py',bundle/'flash.py')
    (bundle/'README.txt').write_text('MST-Link 1.1.0 — ESP32-S3, >=4 MB flash, no PSRAM required\n\n'
        'Use the native USB connector (GPIO19 D-, GPIO20 D+), not a USB-UART connector.\n'
        'Enter ROM download mode with BOOT held during reset, then release BOOT.\n'
        'Install esptool 4.12 or 5.x: python3 -m pip install "esptool>=4.12,<6"\n'
        'Flash: python3 flash.py --port YOUR_SERIAL_PORT\n'
        'The script verifies all images and preserves the NVS settings region.\n'
        'Do not flash the application binary alone at address 0.\n'
        'After flashing reset normally, then connect to MST-Link / mstlink1 and open http://192.168.4.1/.\n'
        'Recovery: ground GPIO4 during application startup, then remove jumper and save new settings.\n'
        'No HU credentials are embedded. Physical HU acceptance is pending.\n'
        'Full instructions: https://github.com/iscle/mst-link/blob/main/docs/esp32s3.md\n')
    archive=ROOT/'dist/mst-link-esp32s3.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for name in ['bootloader.bin','partition-table.bin','mst-link-esp32s3.bin','flash.json','flash.py','README.txt']:z.write(bundle/name,name)
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    (ROOT/'dist/ESP32S3-SHA256SUMS').write_text(digest+'  '+archive.name+'\n')
    print('Built',archive)
