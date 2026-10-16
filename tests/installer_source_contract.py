#!/usr/bin/env python3
"""Portable source audit for the Windows installer (does not compile Setup.exe)."""
from pathlib import Path
import hashlib
import re
import sys

root=Path(__file__).resolve().parents[1]
version=(root/'VERSION').read_text(encoding='utf-8').strip()
errors=[]
if not re.fullmatch(r'\d+\.\d+\.\d+',version): errors.append('Invalid VERSION authority')
iss=(root/'installer/Such.iss').read_text(encoding='utf-8')
builder=(root/'scripts/build_installer_windows.ps1').read_text(encoding='utf-8')
resources=(root/'platform/windows/SuchVersion.rc.in').read_text(encoding='utf-8')
inno=(root/'scripts/windows_inno_setup.ps1').read_text(encoding='utf-8')
inno_assets=(root/'scripts/inno_setup7_assets.txt').read_text(encoding='utf-8')
win_audit=(root/'scripts/audit_windows.ps1').read_text(encoding='utf-8-sig')
version_header=(root/'cmake/Version.h.in').read_text(encoding='utf-8')
cmake=(root/'CMakeLists.txt').read_text(encoding='utf-8')
ignore=(root/'.gitignore').read_text(encoding='utf-8')
checks={
 'Inno version argument': '#ifndef AppVersion' in iss and 'AppVersion={#AppVersion}' in iss,
 'Inno installer runtime required': iss.count('{#StageBin}\\SuchRuntimePrivate.dll')==2,
 'Runtime verified before compiling': 'Assert-SuchWindowsRuntimeSha256' in builder and builder.index('Assert-SuchWindowsRuntimeSha256')<builder.index('Invoke-SuchNative -FilePath $iscc'),
 'No stale staging reuse': 'Invoke-SuchWindowsVerifiedBuild' in builder and 'Test-SuchStageManifest' not in builder,
 'PE metadata verified': 'VersionInfo' in builder and 'ProductVersion' in builder and 'FileVersion' in builder,
 'Per-user no elevation': 'PrivilegesRequired=lowest' in iss and 'DefaultDirName={localappdata}\\Programs\\Such' in iss,
 'No service/autostart': not re.search(r'(?i)\b(schtasks|Register-ScheduledTask|CreateService|RunOnce|StartupFolder)\b',iss),
 'Private runtime source not packaged': 'TopoDB' in iss and 'SuchRuntimePrivate.cpp' not in iss,
 'Installer binary ignored': '/dist/installers/' in ignore and '/Such_v*_Windows_x64_Setup*.exe' in ignore,
 'Windows GUI/CLI PE versions generated': 'SuchVersion.rc.in' in cmake and 'SuchCLI_version.rc' in cmake and 'SuchGUI_version.rc' in cmake,
 'PE authority fields generated': '@PROJECT_VERSION_PATCH@' in resources and '"@PROJECT_VERSION@\\0"' in resources,
 'Inno Setup 7 native x64 setup': 'SetupArchitecture=x64' in iss and 'ArchitecturesAllowed=x64compatible' not in iss,
 'Inno Setup 7 only compiler': 'FileMajorPart -eq 7' in inno and 'Inno Setup 6' not in builder,
 'Inno Setup secure auto provision': all(x in inno for x in ('Invoke-WebRequest', 'Get-FileHash', 'Get-AuthenticodeSignature', 'Pyrsys B', 'Invoke-SuchNative', 'OfflineOnly')),
 'Release asset pinned': 'InnoSetupVersion=7.1.0' in inno_assets and 'InnoSetupSha256=0362a383ed217d4c4239b5933866dd96d3eb2102737da92f80f6057a4b40df2f' in inno_assets,
 'Windows audit generated class contract': 'kWindowClassName' in win_audit and 'Version.h.in' in win_audit and "VersionTag.Substring" not in win_audit and '@PROJECT_VERSION_PATCH@' in version_header,
 'Installer compiler resolver in build': 'windows_inno_setup.ps1' in builder and '-OfflineOnly:$OfflineOnly' in builder,
 'Single installer version source': '$Version=Get-SuchVersion -SourceRoot $Root' in builder and version not in iss and version not in builder,
}
for name,valid in checks.items():
 print(f'{"PASS" if valid else "FAIL"}: {name}')
 if not valid:errors.append(name)
expected={}
for line in (root/'runtime/RUNTIME_SHA256.txt').read_text().splitlines():
 parts=line.split()
 if len(parts)==3:
  arch,file,digest=parts
  expected[(arch,file)]=digest
for arch,name in [('windows-x64','SuchRuntimePrivate.dll'),('linux-x64','libSuchRuntimePrivate.so')]:
 src=root/'runtime'/arch/name
 digest=hashlib.sha256(src.read_bytes()).hexdigest() if src.is_file() else None
 valid=bool(digest) and digest==expected.get((arch,name))
 print(f'{"PASS" if valid else "FAIL"}: pinned {arch} SHA-256')
 if not valid:errors.append(f'{arch} runtime hash mismatch')
if errors:
 print('Installer source contract FAILED:', ', '.join(errors)); sys.exit(1)
print('Installer source contract PASS (static only; Windows ISCC/install are not qualified)')
