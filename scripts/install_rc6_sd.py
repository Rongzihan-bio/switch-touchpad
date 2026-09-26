#!/usr/bin/env python3
"""Install the verified rc6 release on the user's known SD, with local backup."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SD = Path('/Volumes/Untitled')
UUID = '3E6E6425-AD92-3E17-AE3D-5C1FE8743515'
PACKAGE = ROOT/'dist/touchpad-0.1.1-rc6-sd.zip'
PACKAGE_HASH = 'ea7ed711ae4e7ccb6098e412be28bab4dde94266529d7155b07b84b57908be00'
FILES = (
    'atmosphere/contents/4200000054504144/exefs.nsp',
    'switch/.overlays/touchpad.ovl',
    'switch/touchpad-test/touchpad-test.nro',
    'switch/touchpad-diag/touchpad-diag.nro',
)
MODULE = 'atmosphere/contents/4200000054504144'
CONFIG = 'config/touchpad'

def sha(data):
    return hashlib.sha256(data).hexdigest()

def identity():
    info = plistlib.loads(subprocess.check_output(['diskutil','info','-plist',str(SD)]))
    assert info['VolumeUUID'].upper() == UUID, 'Unexpected SD UUID'
    assert info['MountPoint'] == str(SD) and not info['Internal'] and info['WritableVolume']
    assert (SD/'atmosphere/package3').is_file(), 'Missing Atmosphere'
    return info

def no_links(path):
    for item in (path, *path.parents):
        assert not item.is_symlink(), f'Symlink refused: {item}'
        if item == SD:
            break

info = identity()
assert sha(PACKAGE.read_bytes()) == PACKAGE_HASH, 'Release checksum mismatch'
with zipfile.ZipFile(PACKAGE) as z:
    assert z.testzip() is None
    meta = json.loads(z.read('build-info.json'))
    assert meta['version'] == '0.1.1-rc6' and not meta['autostart']
    assert not any(n.endswith('/boot2.flag') for n in z.namelist())
    manifest = dict(line.split('  ',1)[::-1] for line in z.read('SHA256SUMS').decode().splitlines())
    payloads = {name:z.read(name) for name in FILES}
    for name,data in payloads.items():
        assert sha(data) == manifest[name], f'Entry checksum mismatch: {name}'

for name in (*FILES, MODULE, CONFIG, MODULE+'/flags/boot2.flag'):
    no_links(SD/name)
    if name in FILES and (SD/name).exists():
        assert (SD/name).is_file(), f'Unexpected target type: {name}'
assert not (SD/MODULE/'exefs').exists(), 'Unexpected unpacked ExeFS; inspect before replacing'

backup = ROOT/'build/deployment'/datetime.datetime.now().strftime('rc6-install-backup-%Y%m%d-%H%M%S')
backup.mkdir()
saved_hashes = {}
paths = {SD/name for name in FILES if (SD/name).is_file()}
for name in (MODULE, CONFIG):
    directory = SD/name
    if directory.is_dir():
        for path in directory.rglob('*'):
            no_links(path)
            if path.is_file(): paths.add(path)
for source in sorted(paths):
    relative = source.relative_to(SD)
    dest = backup/relative
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,dest)
    digest = sha(source.read_bytes())
    assert sha(dest.read_bytes()) == digest, f'Backup mismatch: {relative}'
    saved_hashes[relative.as_posix()] = digest
(backup/'backup-manifest.json').write_text(json.dumps(saved_hashes,indent=2)+'\n')

# Verify the card again immediately before writing. Only these four files and
# this module's optional autostart flag may change; config and other modules stay.
assert identity()['DeviceIdentifier'] == info['DeviceIdentifier']
flag = SD/MODULE/'flags/boot2.flag'
if flag.exists(): flag.unlink()
installed = {}
for name,data in payloads.items():
    target = SD/name
    target.parent.mkdir(parents=True,exist_ok=True)
    flags = target.parent.stat().st_flags
    if flags & stat.SF_ARCHIVED:
        os.chflags(target.parent,flags & ~stat.SF_ARCHIVED)
    temporary = target.with_name(target.name+'.rc6-update')
    with temporary.open('xb') as out:
        out.write(data)
        out.flush()
        os.fsync(out.fileno())
    os.replace(temporary,target)
    installed[name] = sha(target.read_bytes())
    assert installed[name] == sha(data), f'Installed checksum mismatch: {name}'
for name,digest in saved_hashes.items():
    if name.startswith(CONFIG+'/'):
        assert sha((SD/name).read_bytes()) == digest, 'Configuration or logs changed'
assert not flag.exists()
report = {
    'version':'Touchpad 0.1.1-rc6', 'diagnostic_version':'0.3.0',
    'volume_uuid':UUID, 'device':info['DeviceIdentifier'], 'whole_disk':info['ParentWholeDisk'],
    'package_sha256':PACKAGE_HASH, 'backup':str(backup),
    'installed_files':installed, 'autostart':False,
    'configuration_and_logs_preserved':True, 'hardware_verified':False,
}
(backup/'installation.json').write_text(json.dumps(report,indent=2)+'\n')
subprocess.run(['sync'],check=True)
print(json.dumps(report,indent=2))
