#!/usr/bin/env python3
"""Create a source release from the audited manifest, never from a directory walk.

The public source bundle deliberately retains the separately licensed, hash-
pinned Windows/Linux runtimes. Toolchain caches and build outputs are excluded.
"""

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RUNTIMES = {
    'windows-x64': 'SuchRuntimePrivate.dll',
    'linux-x64': 'libSuchRuntimePrivate.so',
}


def collect() -> tuple[str, list[tuple[str, Path]]]:
    version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Invalid product VERSION')
    lines = (ROOT / 'PACKAGE_MANIFEST.txt').read_text(encoding='utf-8').splitlines()
    names = [line.strip().replace('\\', '/') for line in lines
             if line.strip() and not line.lstrip().startswith('#')]
    if 'PACKAGE_MANIFEST.txt' not in names:
        names.append('PACKAGE_MANIFEST.txt')
    entries = []
    seen = set()
    for name in sorted(names):
        relative = PurePosixPath(name)
        if relative.is_absolute() or not relative.parts or '..' in relative.parts:
            raise ValueError(f'Unsafe manifest entry: {name}')
        if relative.parts[0] in {'.git', '.cache', 'build', 'dist', '.secure', '.toolchain'}:
            raise ValueError(f'Generated dependency is listed in manifest: {name}')
        key = name.casefold()
        if key in seen:
            raise ValueError(f'Duplicate manifest entry: {name}')
        seen.add(key)
        source = ROOT.joinpath(*relative.parts)
        if source.is_symlink() or not source.is_file():
            raise ValueError(f'Missing or unsafe source file: {name}')
        entries.append((name, source))

    hashes = {}
    for line in (ROOT / 'runtime/RUNTIME_SHA256.txt').read_text(encoding='utf-8').splitlines():
        parts = line.split()
        if len(parts) == 3:
            hashes[(parts[0], parts[1])] = parts[2].lower()
    for arch, filename in RUNTIMES.items():
        name = f'runtime/{arch}/{filename}'
        if name.casefold() not in seen:
            raise ValueError(f'Pinned runtime excluded from source release: {name}')
        actual = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
        if actual != hashes.get((arch, filename)):
            raise ValueError(f'Pinned runtime SHA-256 mismatch: {name}')
    return version, entries


def package(output: Path) -> tuple[Path, int]:
    version, entries = collect()
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output in {src.resolve() for _, src in entries}:
        raise ValueError('Output would replace a source file')
    prefix = f'Such_v{version}/'
    tmp = output.with_name(output.name + '.tmp')
    try:
        with zipfile.ZipFile(tmp, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, source in entries:
                info = zipfile.ZipInfo(prefix + name, (2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (0o100755 if source.suffix == '.sh' else 0o100644) << 16
                archive.writestr(info, source.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        with zipfile.ZipFile(tmp) as archive:
            if archive.testzip() is not None or len(archive.namelist()) != len(entries):
                raise ValueError('Generated source ZIP integrity check failed')
        tmp.replace(output)
    finally:
        tmp.unlink(missing_ok=True)
    return output, len(entries)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='Destination ZIP (default: dist/source/Such_vVERSION_SourceOnly.zip)')
    args = parser.parse_args()
    product_version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
    destination = args.output or ROOT / 'dist' / 'source' / f'Such_v{product_version}_SourceOnly.zip'
    path, count = package(destination)
    print(f'Source release PASS: {path} ({count} manifest files, runtimes SHA-256 checked)')
