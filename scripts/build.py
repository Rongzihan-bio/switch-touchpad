#!/usr/bin/env python3
"""Build exclusively with the downloaded, pinned ARM64 image. Never pull implicitly."""
import json
import pathlib
import subprocess
import sys
import datetime
from project_files import compiler_inputs, tree_hash

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENV = ROOT / 'downloads/switch-build-env-20260922'
manifest = json.loads((ROOT / 'toolchain.lock.json').read_text())
image = manifest['image']['digest']
source_hash = tree_hash(compiler_inputs())
run = subprocess.run
if run(['docker', 'info'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
    sys.exit('Docker is not running. Start Docker Desktop, then run make build again.')
if run(['docker', 'image', 'inspect', image], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
    archive = ENV / manifest['image']['file']
    if not archive.is_file():
        sys.exit(f'Pinned image is missing. Import the offline archive from {archive}, then retry. No image was pulled.')
    import hashlib
    with archive.open('rb') as f:
        hasher = hashlib.sha256()
        for block in iter(lambda: f.read(1024*1024), b''): hasher.update(block)
        digest = hasher.hexdigest()
    if digest != manifest['image']['sha256']:
        sys.exit('Toolchain archive SHA-256 mismatch; refusing to load it.')
    run(['docker', 'load', '-i', str(archive)], check=True)
args = ['docker', 'run', '--rm', '--pull', 'never', '--network', 'none', '--platform', 'linux/arm64',
        '--mount', f'type=bind,source={ROOT},target=/work', '-w', '/work', '--entrypoint', '/bin/bash', image]
(ROOT / 'build/switch').mkdir(parents=True, exist_ok=True)
with (ROOT / 'build/switch/build.log').open('w') as log:
    process = subprocess.Popen(args + ['-c', 'set -eu\n/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++ --version\ndkp-pacman -Q libnx devkitA64 switch-tools\nmake -f Makefile.switch -j4\n/opt/devkitpro/devkitA64/bin/aarch64-none-elf-size build/switch/touchpad.elf build/switch/touchpad-overlay.elf build/switch/touchpad-test.elf'],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in process.stdout:
        print(line, end='', flush=True)
        log.write(line)
    if process.wait(): sys.exit(process.returncode)
from check_artifacts import check
check()
if source_hash != tree_hash(compiler_inputs()):
    sys.exit('Compiler inputs changed during the build; rebuild before packaging.')
(ROOT/'build/switch/build-info.json').write_text(json.dumps({
    'version':'0.1.1-rc10', 'built_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'compiler_inputs_sha256':source_hash, 'image_digest':image,
    'packages':{k:manifest['packages'][k] for k in ('devkitA64','libnx','switch-tools','devkita64-gcc')},
    'hardware_verified':False
},indent=2)+'\n')
