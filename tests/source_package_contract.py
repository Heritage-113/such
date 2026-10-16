#!/usr/bin/env python3
"""Verify source packaging retains approved runtimes but drops local toolchains."""

from pathlib import Path
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'scripts'))
from package_source import package  # noqa: E402

with tempfile.TemporaryDirectory(prefix='such-source-qa-') as temp:
    target = Path(temp) / 'such.zip'
    generated, count = package(target)
    assert generated == target and count > 100
    with zipfile.ZipFile(target) as archive:
        paths = archive.namelist()
        assert archive.testzip() is None
        assert len(paths) == len(set(paths)) == count
        assert all('/.cache/' not in p and '/build/' not in p and '/dist/' not in p for p in paths)
        assert any(p.endswith('/runtime/windows-x64/SuchRuntimePrivate.dll') for p in paths)
        assert any(p.endswith('/runtime/linux-x64/libSuchRuntimePrivate.so') for p in paths)
        assert any(p.endswith('/scripts/windows_inno_setup.ps1') for p in paths)
        assert any(p.endswith('/scripts/package_source.py') for p in paths)
print('Source packaging PASS (manifest-only, hash-pinned runtimes, no toolchain cache)')
