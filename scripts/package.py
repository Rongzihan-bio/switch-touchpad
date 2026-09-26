#!/usr/bin/env python3
"""Create matching binary/source releases after structural and host test checks."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile
from check_artifacts import check
from project_files import compiler_inputs, tree_hash, release_sources

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'build/switch'
DIST = ROOT/'dist'
VERSION = '1.0.0-beta.1'
if VERSION == '0.1.0':
    raise SystemExit('Release 0.1.0 is blocked after an on-device boot failure. See docs/BOOT_FAILURE.md; do not redistribute or overwrite this release.')
check()
info = json.loads((OUT/'build-info.json').read_text())
if info['version'] != VERSION:
    raise SystemExit('Build version differs from release version. Run make build first.')
if info['compiler_inputs_sha256'] != tree_hash(compiler_inputs()):
    raise SystemExit('Source changed after the last Switch build. Run make build first.')
test = subprocess.run(['make','test'],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
print(test.stdout,end='')
(ROOT/'build/host/test.log').write_text(test.stdout)
if test.returncode: raise SystemExit(test.returncode)
info['autostart'] = False
info['release_status'] = 'beta; based on user-confirmed rc10; runtime changes limited to version labels'
info['hardware_validation'] = json.loads((ROOT/'docs/releases/1.0.0-beta.1-validation.json').read_text())
info['backend_memory'] = {'physical_pool':'Applet', 'pm_resource_group':'Applet', 'fixed_heap_bytes':4*1024*1024, 'main_stack_bytes':65536}
info['diagnostic_version'] = '0.3.4'
info['host_test_result'] = test.stdout.strip().splitlines()[-1]
paths = release_sources()
source_manifest = {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
info['source_tree_sha256'] = tree_hash(paths)
DIST.mkdir(exist_ok=True)
stage = DIST/'sd'
if stage.exists(): shutil.rmtree(stage)
module = stage/'atmosphere/contents/4200000054504144'
module.mkdir(parents=True)
# Explicitly manual launch: never ship a boot2.flag in this beta release.
shutil.copy2(OUT/'touchpad.nsp',module/'exefs.nsp')
(stage/'switch/.overlays').mkdir(parents=True)
shutil.copy2(OUT/'touchpad.ovl',stage/'switch/.overlays/touchpad.ovl')
(stage/'switch/touchpad-test').mkdir(parents=True)
shutil.copy2(OUT/'touchpad-test.nro',stage/'switch/touchpad-test/touchpad-test.nro')
(stage/'switch/touchpad-diag').mkdir(parents=True)
shutil.copy2(OUT/'touchpad-diag.nro',stage/'switch/touchpad-diag/touchpad-diag.nro')
for name in ('README.md','LICENSE','THIRD_PARTY.md'):
    shutil.copy2(ROOT/name,stage/name)
shutil.copytree(ROOT/'docs',stage/'docs')
for name in ('libnx','libtesla'):
    target = stage/'licenses'/name
    target.mkdir(parents=True)
    for p in (ROOT/'third_party'/name).glob('LICENSE*'): shutil.copy2(p,target/p.name)
(stage/'build-info.json').write_text(json.dumps(info,ensure_ascii=False,indent=2)+'\n')
(stage/'SOURCE-MANIFEST.json').write_text(json.dumps(source_manifest,indent=2)+'\n')
hashes = {p.relative_to(stage).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(stage.rglob('*')) if p.is_file()}
assert not list(stage.rglob('boot2.flag')), 'Manual launch only'
(stage/'SHA256SUMS').write_text(''.join(f'{h}  {p}\n' for p,h in hashes.items()))

def write_entry(z, name, data):
    entry = zipfile.ZipInfo(name,(2026,9,23,0,0,0))
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.external_attr = 0o100644 << 16
    z.writestr(entry,data)

binary_zip = DIST/f'touchpad-{VERSION}-sd.zip'
with zipfile.ZipFile(binary_zip,'w') as z:
    for p in sorted(stage.rglob('*')):
        if p.is_file(): write_entry(z,p.relative_to(stage).as_posix(),p.read_bytes())
source_zip = DIST/f'touchpad-{VERSION}-source.zip'
with zipfile.ZipFile(source_zip,'w') as z:
    prefix = f'touchpad-{VERSION}-source/'
    for p in paths: write_entry(z,prefix+p.relative_to(ROOT).as_posix(),p.read_bytes())
    write_entry(z,prefix+'SOURCE-MANIFEST.json',json.dumps(source_manifest,indent=2).encode()+b'\n')
    write_entry(z,prefix+'BUILD-INFO.json',json.dumps(info,indent=2).encode()+b'\n')
for p in (binary_zip,source_zip):
    with zipfile.ZipFile(p) as z: assert z.testzip() is None
checksums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in (binary_zip,source_zip))
(DIST/'SHA256SUMS').write_text(checksums)
(DIST/f'touchpad-{VERSION}-SHA256SUMS').write_text(checksums)
print(f'Packaged {binary_zip.name} ({binary_zip.stat().st_size} bytes)')
print(f'Packaged {source_zip.name} ({source_zip.stat().st_size} bytes)')
