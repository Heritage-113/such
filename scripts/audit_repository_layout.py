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
ignored_roots = {'.git', '.github', '.cache', '.secure', '.toolchain', 'build', 'dist'}

bad = []
missing = []
basename_owners = {}
casefold_path_owners = {}

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

    # Windows extraction targets are normally case-insensitive. Reject every
    # file-vs-directory or path-vs-path collision after case folding. This
    # catches layouts such as a file named `Packages` beside a directory named
    # `packages/`, which is valid on Linux but cannot be extracted losslessly
    # onto the default Windows filesystem.
    path_key = rel_posix.casefold()
    node_kind = 'dir' if p.is_dir() else 'file' if p.is_file() else 'other'
    previous = casefold_path_owners.get(path_key)
    if previous is not None and previous != (rel_posix, node_kind):
        bad.append(
            f'case-insensitive-path-collision:{previous[0]}[{previous[1]}]'
            f'<->{rel_posix}[{node_kind}]'
        )
    else:
        casefold_path_owners[path_key] = (rel_posix, node_kind)

    if p.is_file():
        if rel_posix not in manifest:
            bad.append(rel_posix)
        else:
            # Some archive/extraction stacks mishandle same-basename files even
            # when their parent directories differ. Public release payloads
            # therefore require a case-insensitive globally unique basename.
            key = p.name.casefold()
            previous = basename_owners.get(key)
            if previous is not None and previous != rel_posix:
                bad.append(f'duplicate-basename:{previous}<->{rel_posix}')
            else:
                basename_owners[key] = rel_posix

for entry in sorted(manifest):
    if not (root / Path(entry)).is_file():
        missing.append(entry)


# Cross-platform script architecture contract.
version_file = root / 'VERSION'
if not version_file.is_file():
    bad.append('missing-version-authority:VERSION')
    release_version = ''
else:
    release_version = version_file.read_text(encoding='utf-8').strip()
    import re
    if not re.fullmatch(r'\d+\.\d+\.\d+', release_version):
        bad.append(f'invalid-version-authority:{release_version}')

cmake_text = (root / 'CMakeLists.txt').read_text(encoding='utf-8') if (root / 'CMakeLists.txt').is_file() else ''
if 'file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"' not in cmake_text or 'project(Such VERSION "${SUCH_VERSION_TEXT}"' not in cmake_text:
    bad.append('cmake-version-authority-drift')

# The product version is a single authority. Never reintroduce a hardcoded
# Win32 class identity or a runtime hash-manifest name tied to this version.
win_source = (root / 'platform/windows/Win32Frontend.cpp')
if win_source.is_file():
    win_text = win_source.read_text(encoding='utf-8')
    if 'such::version::kWindowClassName' not in win_text or 'HeritageSuchV130Window' in win_text:
        bad.append('win32-versioned-class-identity-drift')
version_template = root / 'cmake/Version.h.in'
if version_template.is_file():
    text_version = version_template.read_text(encoding='utf-8')
    for token in ('@PROJECT_VERSION@', '@PROJECT_VERSION_MAJOR@', '@PROJECT_VERSION_MINOR@', '@PROJECT_VERSION_PATCH@'):
        if token not in text_version:
            bad.append(f'generated-version-template-drift:{token}')
if not (root / 'runtime/RUNTIME_SHA256.txt').is_file():
    bad.append('runtime-sha-authority-missing')
for script_path in (root / 'scripts/windows_paths.ps1', root / 'scripts/linux_build_common.sh'):
    if script_path.is_file() and 'RUNTIME_SHA256_v' in script_path.read_text(encoding='utf-8-sig'):
        bad.append(f'versioned-runtime-sha-manifest-path:{script_path.name}')

scripts = root / 'scripts'
required_architecture_files = [
    scripts / 'windows_pipeline.ps1', scripts / 'windows_entry_common.ps1',
    scripts / 'windows_powershell_host.cmd', scripts / 'windows_powershell_host.ps1', scripts / 'windows_powershell_assets.txt',
    scripts / 'linux_pipeline.sh', scripts / 'linux_artifact_state.sh',
    scripts / 'ipados_pipeline.sh', scripts / 'ipados_common.sh',
    root / 'docs' / 'SCRIPT_ARCHITECTURE.md',
]
for path in required_architecture_files:
    if not path.is_file():
        bad.append(f'missing-script-architecture-file:{path.relative_to(root).as_posix()}')

# Windows: atomic steps never invoke siblings/public entries. Public entries use
# windows_pipeline.ps1 and do not embed direct atomic script paths.
windows_steps = sorted(scripts.glob('windows_step_*.ps1'))
for path in windows_steps:
    text = path.read_text(encoding='utf-8')
    for sibling in windows_steps:
        if sibling.name != path.name and sibling.name in text:
            bad.append(f'windows-atomic-step-recursion:{path.name}->{sibling.name}')
    if '[SUCH:FATAL]' in text or '\nexit ' in text or 'trap {' in text:
        bad.append(f'windows-atomic-error-wrapper:{path.name}')

windows_entries = ['build_windows.ps1','verify_windows.ps1','install_windows.ps1','release_windows.ps1','qualify_windows.ps1']
for name in windows_entries:
    path=scripts/name
    if not path.is_file():
        bad.append(f'missing-windows-entry:{name}')
        continue
    text=path.read_text(encoding='utf-8')
    if 'windows_pipeline.ps1' not in text:
        bad.append(f'windows-entry-bypasses-pipeline:{name}')
    if 'windows_step_' in text:
        bad.append(f'windows-entry-embeds-atomic-step:{name}')

# Only the host materializer is a Windows PowerShell 5.1 bridge. Main workflows
# are launched under the pinned PowerShell 7 host. Native exit-state hazards
# remain forbidden across all scripts.
host_bridge = scripts / 'windows_powershell_host.ps1'
if host_bridge.is_file():
    bridge_text = host_bridge.read_text(encoding='utf-8-sig')
    if '.ArgumentList' in bridge_text or '[Environment+SpecialFolder]' in bridge_text:
        bad.append('windows-powershell51-host-bridge-api-drift')
    if any(token in bridge_text for token in ('1u','2u','3u','4u','5u','6u','7u','8u','9u','0u')):
        bad.append('windows-powershell51-host-bridge-literal-drift')
for ps_path in sorted(scripts.glob('*.ps1')):
    if ps_path.name == 'audit_windows.ps1':
        continue
    ps_text = ps_path.read_text(encoding='utf-8-sig')
    code_only='\n'.join(line for line in ps_text.splitlines() if not line.lstrip().startswith('#'))
    if '$LASTEXITCODE' in code_only:
        bad.append(f'windows-stale-lastexitcode-hazard:{ps_path.name}')

windows_cmd_entries = {
    'bootstrap_windows.cmd':'bootstrap_windows_entry.ps1',
    'build_windows.cmd':'build_windows.ps1',
    'doctor_windows.cmd':'doctor_windows.ps1',
    'install_windows.cmd':'install_windows.ps1',
    'prepare_windows_offline_toolchain.cmd':'prepare_windows_offline_toolchain.ps1',
    'qualify_windows.cmd':'qualify_windows.ps1',
    'release_windows.cmd':'release_windows.ps1',
    'uninstall_windows.cmd':'uninstall_windows.ps1',
    'verify_windows.cmd':'verify_windows.ps1',
    'build_installer_windows.cmd':'build_installer_windows.ps1',
}
for cmd_name, entry_name in windows_cmd_entries.items():
    cmd_path=scripts/cmd_name
    if not cmd_path.is_file():
        bad.append(f'missing-windows-cmd-entry:{cmd_name}')
        continue
    cmd_text=cmd_path.read_text(encoding='utf-8')
    if 'windows_powershell_host.cmd' not in cmd_text or entry_name not in cmd_text or 'WindowsPowerShell\\v1.0' in cmd_text:
        bad.append(f'windows-central-powershell-host-drift:{cmd_name}')
    if 'windows_cmd_finish.cmd' not in cmd_text:
        bad.append(f'windows-cmd-diagnostic-window-drift:{cmd_name}')

powershell_assets = scripts / 'windows_powershell_assets.txt'
if powershell_assets.is_file():
    metadata = {}
    for raw in powershell_assets.read_text(encoding='utf-8-sig').splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if '=' not in line:
            bad.append('windows-powershell-assets-invalid-line')
            continue
        key, value = line.split('=', 1)
        metadata[key] = value
    version = metadata.get('PowerShellVersion', '')
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        bad.append('windows-powershell-version-invalid')
    for label, arch in (('X64','x64'),('Arm64','arm64'),('X86','x86')):
        zip_name = metadata.get(f'PowerShell{label}Zip', '')
        digest = metadata.get(f'PowerShell{label}Sha256', '')
        url = metadata.get(f'PowerShell{label}Url', '')
        expected_name = f'PowerShell-{version}-win-{arch}.zip'
        expected_prefix = f'https://github.com/PowerShell/PowerShell/releases/download/v{version}/'
        if zip_name != expected_name:
            bad.append(f'windows-powershell-asset-name-drift:{arch}')
        if not re.fullmatch(r'[0-9A-Fa-f]{64}', digest):
            bad.append(f'windows-powershell-asset-sha-invalid:{arch}')
        if url != expected_prefix + expected_name:
            bad.append(f'windows-powershell-asset-url-drift:{arch}')

host_materializer = scripts / 'windows_powershell_host.ps1'
if host_materializer.is_file():
    host_text = host_materializer.read_text(encoding='utf-8-sig')
    for token in ('Get-FileHash','Expand-Archive','Invoke-WebRequest','windows_powershell_assets.txt'):
        if token not in host_text:
            bad.append(f'windows-powershell-host-materializer-drift:{token}')

step_install_path = scripts / 'windows_step_install.ps1'
if step_install_path.is_file():
    install_text = step_install_path.read_text(encoding='utf-8-sig')
    for token in ('windows_powershell_host.cmd','windows_powershell_host.ps1','windows_powershell_assets.txt','uninstall_windows.cmd'):
        if token not in install_text:
            bad.append(f'windows-install-powershell-host-drift:{token}')

# Installer inputs must remain derived from current version authority and
# verified Windows binaries. A source-only audit cannot replace ISCC smoke.
installer_iss = root / 'installer' / 'Such.iss'
installer_entry = scripts / 'build_installer_windows.ps1'
if not installer_iss.is_file() or not installer_entry.is_file():
    bad.append('windows-installer-definition-missing')
else:
    installer_text=installer_iss.read_text(encoding='utf-8')
    installer_script=installer_entry.read_text(encoding='utf-8-sig')
    if '#ifndef AppVersion' not in installer_text or '$Version=Get-SuchVersion' not in installer_script:
        bad.append('windows-installer-version-not-derived')
    if 'Invoke-SuchWindowsVerifiedBuild' not in installer_script or 'Assert-SuchWindowsRuntimeSha256' not in installer_script:
        bad.append('windows-installer-bypasses-verification')
    if 'PrivilegesRequired=lowest' not in installer_text:
        bad.append('windows-installer-must-be-per-user')

# Windows install/release must be artifact-first. Cross-platform stage naming does
# not justify an unconditional rebuild/test on these heavyweight entry points.
for name in ('install_windows.ps1','release_windows.ps1'):
    path=scripts/name
    if path.is_file():
        text=path.read_text(encoding='utf-8')
        if 'Test-SuchStageManifest' not in text or 'Test-SuchVerificationManifest' not in text:
            bad.append(f'windows-artifact-first-orchestration-missing:{name}')

# Linux atomic steps must not invoke sibling atomic steps or public workflows.
linux_steps = sorted(scripts.glob('linux_step_*.sh'))
linux_public = {'build_linux.sh','verify_linux.sh','qualify_linux.sh','install_linux.sh','release_linux.sh','build_deb.sh'}
for path in linux_steps:
    text=path.read_text(encoding='utf-8')
    for sibling in linux_steps:
        if sibling.name != path.name and sibling.name in text:
            bad.append(f'linux-atomic-step-recursion:{path.name}->{sibling.name}')
    for public_name in linux_public:
        if (f'$SCRIPT_DIR/{public_name}' in text or f'scripts/{public_name}' in text):
            bad.append(f'linux-atomic-calls-public:{path.name}->{public_name}')

# No build/test/package ownership drift in the Linux atomic steps.
linux_build = (scripts/'linux_step_build.sh').read_text(encoding='utf-8') if (scripts/'linux_step_build.sh').is_file() else ''
linux_stage = (scripts/'linux_step_stage.sh').read_text(encoding='utf-8') if (scripts/'linux_step_stage.sh').is_file() else ''
linux_release = (scripts/'linux_step_release.sh').read_text(encoding='utf-8') if (scripts/'linux_step_release.sh').is_file() else ''
linux_deb = (scripts/'linux_step_deb.sh').read_text(encoding='utf-8') if (scripts/'linux_step_deb.sh').is_file() else ''
if 'ctest' in linux_build or '--install' in linux_build or 'dpkg-deb' in linux_build:
    bad.append('linux-build-step-role-drift')
if 'cmake --build' in linux_stage or 'ctest' in linux_stage:
    bad.append('linux-stage-step-role-drift')
if 'linux_step_build' in linux_release or 'linux_step_build' in linux_deb:
    bad.append('linux-package-rebuild-role-drift')

for name in linux_public:
    path=scripts/name
    if not path.is_file():
        bad.append(f'missing-linux-entry:{name}')
        continue
    text=path.read_text(encoding='utf-8')
    if 'linux_pipeline.sh' not in text:
        bad.append(f'linux-entry-bypasses-pipeline:{name}')

# iPadOS mirrors the same ownership model through build/test/stage. It has no
# desktop install step by design.
ipad_steps = sorted(scripts.glob('ipados_step_*.sh'))
for path in ipad_steps:
    text=path.read_text(encoding='utf-8')
    for sibling in ipad_steps:
        if sibling.name != path.name and sibling.name in text:
            bad.append(f'ipados-atomic-step-recursion:{path.name}->{sibling.name}')
for name in ('build_ipados.sh','verify_ipados.sh','qualify_ipados.sh'):
    path=scripts/name
    if not path.is_file():
        bad.append(f'missing-ipados-entry:{name}')
    elif 'ipados_pipeline.sh' not in path.read_text(encoding='utf-8'):
        bad.append(f'ipados-entry-bypasses-pipeline:{name}')

# Active release strings in script implementation should come from VERSION.
if release_version:
    literal = release_version
    allowed_literal_files = {
        'BUILDING.md', # documentation may describe the current release explicitly
    }
    for path in scripts.rglob('*'):
        if not path.is_file() or path.name in allowed_literal_files:
            continue
        if path.suffix.lower() not in {'.sh','.ps1','.cmd','.py'}:
            continue
        text=path.read_text(encoding='utf-8',errors='replace')
        if literal in text:
            bad.append(f'script-active-version-literal:{path.relative_to(root).as_posix()}')

if bad or missing:
    print('Public repository layout rejected:')
    for item in sorted(set(bad)):
        print('  extra:', item)
    for item in missing:
        print('  missing:', item)
    sys.exit(2)

print(f'public repository layout PASS ({len(manifest)} manifest files)')
