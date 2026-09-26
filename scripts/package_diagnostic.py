#!/usr/bin/env python3
"""Package the standalone diagnostic without overwriting any Touchpad release."""
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
from check_artifacts import check
from project_files import compiler_inputs, tree_hash, release_sources
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'build/switch'
DIST = ROOT/'dist'
VERSION = '0.3.4'
check()
info = json.loads((OUT/'build-info.json').read_text())
if info['compiler_inputs_sha256'] != tree_hash(compiler_inputs()):
    raise SystemExit('Compiler inputs changed: rebuild before packaging diagnostic.')
info.update(product='Touchpad Diag', version=VERSION, hardware_verified=False, autostart=False)
DIST.mkdir(exist_ok=True)
standalone = DIST/f'touchpad-diag-{VERSION}.nro'
shutil.copyfile(OUT/'touchpad-diag.nro', standalone)
paths = release_sources()
manifest = {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
entries = {
    'switch/touchpad-diag/touchpad-diag.nro':standalone.read_bytes(),
    'atmosphere/contents/4200000054505052/exefs.nsp':(OUT/'touchpad-probe.nsp').read_bytes(),
    'Touchpad-Diag-README.md':(ROOT/'docs/DIAGNOSTIC.md').read_bytes(),
    'licenses/Touchpad-LICENSE':(ROOT/'LICENSE').read_bytes(),
    'licenses/libnx-LICENSE.md':(ROOT/'third_party/libnx/LICENSE.md').read_bytes(),
    'BUILD-INFO.json':(json.dumps(info,indent=2)+'\n').encode(),
    'SOURCE-MANIFEST.json':(json.dumps(manifest,indent=2)+'\n').encode(),
}
def archive(path, items):
    with zipfile.ZipFile(path,'w') as z:
        for name,data in sorted(items.items()):
            entry = zipfile.ZipInfo(name,(2026,9,23,0,0,0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            z.writestr(entry,data)
    with zipfile.ZipFile(path) as z:
        if z.testzip() is not None: raise RuntimeError('ZIP CRC check failed')
binary = DIST/f'touchpad-diag-{VERSION}-sd.zip'
archive(binary,entries)
source = DIST/f'touchpad-diag-{VERSION}-source.zip'
prefix = f'touchpad-diag-{VERSION}-source/'
source_entries = {prefix+p.relative_to(ROOT).as_posix():p.read_bytes() for p in paths}
source_entries[prefix+'SOURCE-MANIFEST.json'] = entries['SOURCE-MANIFEST.json']
source_entries[prefix+'BUILD-INFO.json'] = entries['BUILD-INFO.json']
archive(source,source_entries)
(DIST/f'touchpad-diag-{VERSION}-SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in (standalone,binary,source)))
print('PASS: diagnostic NRO plus manual-launch probe and matching source packaged; existing plugin releases untouched')
print(binary)
