"""Package only explicit mod/source files; never walk the working directory."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.3.3'
DOCS = ['README.md', 'GUIDE_RU.md', 'BUILDING.md', 'CHANGELOG.md', 'NOTICE.md', 'LICENSE']
SCRIPTS = ['Build-Mod.ps1', 'Build-Installer.ps1', 'Fetch-Sources.ps1', 'prepare_fork.py', 'build_guest_patch.py', 'package_release.py']
TESTS = ['projection.cpp', 'projection.vcxproj', 'GroundPlacement.java', 'InstallerCompatibility.cs', 'GuestIntegration.java']
source_files = [ROOT/x for x in DOCS + ['.gitignore']]
source_files += [ROOT/'scripts'/x for x in SCRIPTS]
source_files += [ROOT/'tests'/x for x in TESTS]
for folder, suffixes in [('src', {'.h', '.cpp', '.java', '.inc'}), ('installer', {'.cs', '.json', '.manifest', '.config'}), ('licenses', {'.txt', '.rst'})]:
    source_files += sorted(p for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in suffixes)
source_files += [ROOT/'installer/assets/README.md', ROOT/'installer/assets/theme.mp3']
assert len(source_files) == len(set(source_files))
source = ROOT/'release-source'
source.mkdir(exist_ok=True)
expected = {p.relative_to(ROOT).as_posix() for p in source_files}
# Refuse stale/unreviewed files instead of silently publishing or deleting them.
stale = {p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()} - expected
if stale:
    raise RuntimeError(f'Unexpected files in source staging: {sorted(stale)}')
tree = []
for file in source_files:
    relative = file.relative_to(ROOT).as_posix()
    data = file.read_bytes()
    content = None if file.suffix == '.mp3' else data.decode('utf-8-sig')
    if content is not None and re.search(r'(?i)users[\\/]+lolol|gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}', content):
        raise RuntimeError(f'Personal path or credential pattern in {relative}')
    dest = source/relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    if content is not None:
        tree.append({'path': relative, 'mode': '100644', 'type': 'blob', 'content': content})

dist = ROOT/'dist'
package = dist/f'RE4Craft-{VERSION}'
package.mkdir(parents=True, exist_ok=True)
exe = package/'RE4Craft-Setup.exe'
if not exe.is_file():
    raise RuntimeError('Build the release installer first.')
for name in DOCS:
    shutil.copyfile(ROOT/name, package/name)
shutil.copytree(ROOT/'licenses', package/'licenses', dirs_exist_ok=True)
config = package/'RE4Craft-Setup.exe.config'
if not config.is_file():
    raise RuntimeError('Installer DPI configuration is missing.')
payload = [exe, config] + [package/x for x in DOCS] + sorted(p for p in (package/'licenses').iterdir() if p.is_file())
expected_package = {p.relative_to(package).as_posix() for p in payload} | {'SHA256SUMS.txt'}
if {p.relative_to(package).as_posix() for p in package.rglob('*') if p.is_file()} - expected_package:
    raise RuntimeError('Unexpected files in release staging.')
sums = '\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(package).as_posix() for p in payload)+'\n'
(package/'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
release_zip = dist/f'RE4Craft-{VERSION}.zip'
with zipfile.ZipFile(release_zip, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in payload + [package/'SHA256SUMS.txt']:
        z.write(p, p.relative_to(package).as_posix())
source_zip = dist/f'RE4Craft-{VERSION}-source.zip'
with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in source_files:
        z.write(p, p.relative_to(ROOT).as_posix())
external_sums = dist/'SHA256SUMS.txt'
external_sums.write_text('\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name for p in (release_zip, source_zip, exe))+'\n', encoding='utf-8')
(ROOT/'build/publish-tree.json').write_text(json.dumps(tree, ensure_ascii=False), encoding='utf-8')
print(json.dumps({'source_files': len(source_files), 'binary_source_files': ['installer/assets/theme.mp3'], 'source_bytes': sum(p.stat().st_size for p in source_files), 'release_zip': str(release_zip), 'source_zip': str(source_zip), 'sha256': hashlib.sha256(release_zip.read_bytes()).hexdigest()}, indent=2))
