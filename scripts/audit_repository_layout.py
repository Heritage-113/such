#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
manifest_path = root / 'PACKAGE_MANIFEST.txt'
if not manifest_path.is_file():
    print('Public repository layout rejected:')
    print('   missing PACKAGE_MANIFEST.txt')
    sys.exit(2)

manifest = {
    line.strip().replace('\\', '/')
    for line in manifest_path.read_text(encoding='utf-8').splitlines()
    if line.strip() and not line.lstrip().startswith('#')
}
manifest.add('PACKAGE_MANIFEST.txt')
allowed_roots = {Path(entry).parts[0] for entry in manifest}

# Repository/CI metadata and generated/local outputs are not distributable
# package payload. Keep them outside PACKAGE_MANIFEST instead of forcing the
# product manifest to know about GitHub Actions implementation details.
ignored_roots = {'.git', '.github', '.cache', '.runtime', 'build', 'dist'}

bad = []
missing = []

for child in root.iterdir():
    if child.name in ignored_roots:
        continue
    if child.is_file() and child.name.startswith('Such_v') and child.suffix.lower() == '.zip':
        continue
    if child.name not in allowed_roots:
        bad.append(child.name)

for p in root.rglob('*'):
    rel = p.relative_to(root)
    if not rel.parts or rel.parts[0] in ignored_roots:
        continue
    rel_posix = rel.as_posix()
    if len(rel.parts) == 1 and p.is_file() and p.name.startswith('Such_v') and p.suffix.lower() == '.zip':
        continue
    if p.is_symlink():
        bad.append(f'symlink:{rel_posix}')
        continue
    if p.is_file() and rel_posix not in manifest:
        bad.append(rel_posix)

for entry in sorted(manifest):
    if not (root / Path(entry)).is_file():
        missing.append(entry)

if bad or missing:
    print('Public repository layout rejected:')
    for item in sorted(set(bad)):
        print('  extra:', item)
    for item in missing:
        print('  missing:', item)
    sys.exit(2)

print(f'public repository layout PASS ({len(manifest)} manifest files)')
