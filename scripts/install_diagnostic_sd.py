#!/usr/bin/env python3
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
EXPECTED_UUID = '3E6E6425-AD92-3E17-AE3D-5C1FE8743515'
PACKAGE = ROOT/'dist/touchpad-diag-0.2.0-sd.zip'
STAGE = ROOT/'build/deployment/diag-stage-0.2.0'
entries = {
    Path('switch/touchpad-diag/touchpad-diag.nro'): 'switch/touchpad-diag/touchpad-diag.nro',
    Path('atmosphere/contents/4200000054505052/exefs.nsp'): 'atmosphere/contents/4200000054505052/exefs.nsp',
}
info = plistlib.loads(subprocess.check_output(['diskutil','info','-plist',str(SD)]))
assert info['VolumeUUID'].upper() == EXPECTED_UUID, 'Unexpected SD card UUID'
assert info['MountPoint'] == str(SD), 'Unexpected mount point'
assert info['DeviceIdentifier'] == 'disk12s1', 'Unexpected device identifier'
assert (SD/'atmosphere/package3').is_file(), 'Atmosphere package3 not found'
assert PACKAGE.is_file()
STAGE.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(PACKAGE) as archive:
    for relative, archive_name in entries.items():
        output = STAGE/relative
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(archive.read(archive_name))
targets = [SD/relative for relative in entries]
for target in targets:
    assert not target.is_symlink(), f'Refusing symlink target: {target}'
    for parent in target.parents:
        if parent == SD.parent:
            break
        assert not parent.is_symlink(), f'Refusing symlink parent: {parent}'
backup = ROOT/'build/deployment'/datetime.datetime.now().strftime('diag-backup-%Y%m%d-%H%M%S')
backup.mkdir()
old = {}
for relative in entries:
    source = SD/relative
    if source.is_file():
        saved = backup/relative
        saved.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, saved)
        old[relative.as_posix()] = hashlib.sha256(saved.read_bytes()).hexdigest()
installed = {}
for relative in entries:
    target = SD/relative
    target.parent.mkdir(parents=True, exist_ok=True)
    flags = target.parent.stat().st_flags
    if flags & stat.SF_ARCHIVED:
        os.chflags(target.parent, flags & ~stat.SF_ARCHIVED)
    data = (STAGE/relative).read_bytes()
    temporary = target.with_name(target.name+'.touchpad-diag-update')
    with temporary.open('wb') as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, target)
    digest = hashlib.sha256(data).hexdigest()
    assert hashlib.sha256(target.read_bytes()).hexdigest() == digest
    installed[relative.as_posix()] = digest
probe_dir = SD/'atmosphere/contents/4200000054505052'
assert not (probe_dir/'flags/boot2.flag').exists(), 'Probe autostart flag must not exist'
report = {
    'version': 'Touchpad Diag 0.2.0',
    'volume_uuid': info['VolumeUUID'],
    'device': info['DeviceIdentifier'],
    'backup': str(backup),
    'old_files': old,
    'installed_files': installed,
    'touchpad_backend_untouched': True,
    'boot2_flag_absent_for_probe': True,
    'hardware_verified': False,
}
(backup/'installation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
