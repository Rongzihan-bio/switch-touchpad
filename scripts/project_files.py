import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CODE_DIRS = ('common','touch','sysmodule','overlay','tester','diagnostic','probe','third_party')
def compiler_inputs():
    paths = [ROOT/'Makefile.switch']
    for directory in CODE_DIRS:
        paths += [p for p in (ROOT/directory).rglob('*') if p.is_file() and p.suffix in ('.cpp','.hpp','.h','.json')]
    return sorted(paths)

def tree_hash(paths):
    h = hashlib.sha256()
    for path in sorted(paths):
        h.update(path.relative_to(ROOT).as_posix().encode()+b'\0')
        h.update(path.read_bytes())
    return h.hexdigest()

def release_sources():
    paths = [ROOT/name for name in ('.gitignore','Agent.md','LICENSE','README.md','THIRD_PARTY.md','Makefile','Makefile.switch','toolchain.lock.json')]
    for directory in CODE_DIRS + ('docs','scripts','tests'):
        paths += [p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.name != '.DS_Store']
    return sorted(paths)
