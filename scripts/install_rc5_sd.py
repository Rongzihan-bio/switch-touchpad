#!/usr/bin/env python3
"""Safely install the memory-reduced manual-launch backend on the known SD card."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SD = Path('/Volumes/Untitled')
EXPECTED_UUID = '3E6E6425-AD92-3E17-AE3D-5C1FE8743515'
MODULE = Path('atmosphere/contents/4200000054504144/exefs.nsp')
OVERLAY = Path('switch/.overlays/touchpad.ovl')
FILES = {
    MODULE: ROOT/'build/switch/touchpad.nsp',
    OVERLAY: ROOT/'build/switch/touchpad.ovl',
}
EXPECTED_OLD = {
    MODULE: '4b460799d11a864f7671286765425e0d733f7b570f8082b06c6d2b3d821391c6',
    OVERLAY: '399c29cc0e1b00e72f024390c1d0ba1007582b8964ce7cc740798a6fcd2e819e',
}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

info = plistlib.loads(subprocess.check_output(['diskutil','info','-plist',str(SD)]))
assert info['VolumeUUID'].upper() == EXPECTED_UUID, 'Unexpected SD card UUID'
assert info['MountPoint'] == str(SD) and info['DeviceIdentifier'] == 'disk12s1'
assert (SD/'atmosphere/package3').is_file(), 'Atmosphere package3 not found'
assert not (SD/'atmosphere/contents/4200000054504144/flags/boot2.flag').exists(), 'Refusing an autostarting installation'
for relative, source in FILES.items():
    assert source.is_file(), f'Missing build output: {source}'
    target = SD/relative
    assert target.is_file() and not target.is_symlink(), f'Unexpected existing target: {target}'
    assert digest(target) == EXPECTED_OLD[relative], f'Existing file changed since diagnostic: {relative}'
    for parent in relative.parents:
        assert not (SD/parent).is_symlink(), f'Refusing symlink parent: {parent}'

backup = ROOT/'build/deployment'/datetime.datetime.now().strftime('rc4-backup-%Y%m%d-%H%M%S')
backup.mkdir()
old = {}
for relative in FILES:
    saved = backup/relative
    saved.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(SD/relative, saved)
    old[relative.as_posix()] = digest(saved)
    assert old[relative.as_posix()] == EXPECTED_OLD[relative]

installed = {}
for relative, source in FILES.items():
    target = SD/relative
    data = source.read_bytes()
    flags = target.parent.stat().st_flags
    if flags & stat.SF_ARCHIVED:
        os.chflags(target.parent, flags & ~stat.SF_ARCHIVED)
    temporary = target.with_name(target.name+'.rc5-update')
    with temporary.open('wb') as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, target)
    installed[relative.as_posix()] = hashlib.sha256(data).hexdigest()
    assert digest(target) == installed[relative.as_posix()]

report = {
    'version': 'Touchpad 0.1.1-rc5',
    'volume_uuid': info['VolumeUUID'],
    'device': info['DeviceIdentifier'],
    'backup': str(backup),
    'old_files': old,
    'installed_files': installed,
    'autostart': False,
    'user_configuration_preserved': True,
    'hardware_verified': False,
}
(backup/'installation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
