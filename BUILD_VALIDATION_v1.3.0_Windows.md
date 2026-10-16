# Such v1.3.0 Windows qualification

Date: 2026-09-26

## Install contract

Such v1.3.0 is a current-user installation.

```text
%LOCALAPPDATA%\Programs\Such\
  Such.exe
  SuchCLI.exe
  SuchRuntimePrivate.dll
  cli\such.exe
```

Registration is current-user only:

- App Paths: `HKCU\Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe`
- uninstall: `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\HeritageSuch`
- uninstall legacy cleanup allowlist: `C:\Heritage\Such`, HKLM App Paths/Uninstall, Common Start Menu/Desktop shortcuts, and `C:\Heritage\Such\cli` in Machine PATH; elevation only when detected
- PATH: User PATH only
- Start Menu: current user's Programs folder
- optional Desktop shortcut: current user's Desktop

The installer must not request UAC, start an Administrator PowerShell, write HKLM, or mutate the machine PATH.

## Native qualification commands

From the extracted v1.3.0 SourceOnly root:

```bat
scripts\doctor_windows.cmd
scripts\build_windows.cmd -Config Release -Arch x64 -Clean
scripts\verify_windows.cmd -Config Release -Arch x64 -Clean -RequireRuntime
scripts\install_windows.cmd -Config Release -Arch x64 -Clean -SkipFonts -RequireRuntime
```

The verification command is expected to exercise:

- repository and Windows source audit
- CMake + Visual Studio 2022 generator selection
- MSVC `/W4 /WX` build
- public CTest suite
- runtime architecture and pinned SHA-256
- ABI stub CLI/GUI smoke
- real private-runtime CLI/GUI smoke
- real `/inside` fixture search
- non-resident GUI smoke exit

## MSVC-specific source gates

The product source rejects or avoids:

- `ProcessStartInfo.ArgumentList`
- `ProcessStartInfo.Environment`
- direct `FARPROC` -> typed function-pointer casts
- `SetWindowLongPtr` WNDPROC subclass casts
- non-CALLBACK `EnumWindows` thunks
- Windows wide path keys passed to UTF-8 policy APIs

## Build-script baseline

v1.3.0 keeps generated Windows build/stage/verify state out of the checkout in a short per-user workspace. The source tree remains authoritative input, while generated state is owned by `%USERPROFILE%\SW` (or an explicit `SUCH_WINDOWS_WORK_ROOT` / `SUCH_BUILD_ROOT`). Long source paths are exposed to CMake through a short workspace junction when required.

The pipeline is now responsibility-split:

- `bootstrap_windows.ps1`: dependencies only;
- `windows_step_build.ps1`: configure/build only;
- `windows_step_test.ps1`: CTest plus ABI/production-runtime smoke only;
- `windows_step_stage.ps1`: toolchain-free copy of SHA-verified artifacts to staging only;
- `windows_step_install.ps1`: staged artifacts to current-user install only;
- `windows_step_release.ps1`: staged artifacts to release ZIP only.

Public build/verify/install/release scripts only orchestrate those steps, but each public workflow is independently runnable. `verify_windows` performs audit -> bootstrap -> build -> test; `install_windows` continues through stage -> install; `release_windows` continues through stage -> release. Callers do not need to pre-create `verified.json` or `staged.json`. Cross-platform repository audit and `audit_windows.ps1` reject responsibility drift. Test/stage handoff remains SHA-256-attested through `verified.json` and `staged.json`; rebuild invalidates prior attestations. `doctor_windows.ps1` is diagnostic-only.

## Index-process boundary

Win32 must not perform a disk crawl in the GUI process. `/drive`, `/index`, startup rebuild, and explicit reindex launch sibling `SuchCLI.exe --index-worker ...` with `CREATE_NO_WINDOW`. The worker owns `IndexRootService`, waits for indexing to become idle, drains bounded maintenance, and exits.

## Status

On the current refactor host, the shared C++ tree passes the strict Linux build and CTest 13/13, and the full Linux qualification remains PASS. The worker command and launcher retain dedicated smoke tests. The Windows pipeline ownership contract passes the cross-platform repository audit. Native MSVC/Windows PowerShell execution remains Platform-unverified here.

**Native MSVC/Windows PowerShell execution still requires the final Windows-host run above.** Do not label the Windows release qualified until that exact SourceOnly tree passes it.

## v1.3.0 Unicode / IME gate

Windows qualification must additionally verify Korean/non-ASCII filename search from both the desktop search EDIT and `SuchCLI`. The source audit requires native IME composition handling (`WM_IME_STARTCOMPOSITION` / `WM_IME_ENDCOMPOSITION`) and a wide-command-line-to-UTF-8 CLI entry boundary.

Manual desktop check:

1. index a folder containing `한글검색테스트.txt` and `회의록_프로젝트.pdf`;
2. type `한글`, `검색테스트`, and `회의록` with the Windows Korean IME;
3. confirm the matching rows appear after composition commits;
4. run `SuchCLI.exe 한글` from a Unicode-capable terminal and confirm the same file is returned.

## Dependency bootstrap update

Implemented in the v1.3.0 source package:

- pinned portable CMake 3.31.12 with SHA-256 verification;
- automatic Visual Studio 2022 Build Tools C++ workload provisioning when MSVC is absent;
- official Visual Studio network-layout preparation for disconnected machines;
- `--noWeb` installation from an external short Visual Studio layout (`%USERPROFILE%\STC\offline\VS17` by default);
- portable CMake/toolchain cache outside the source tree (`%USERPROFILE%\STC`);
- source-tree-independent build/stage/verify workspace (`%USERPROFILE%\SW`);
- legacy in-repository VS layout detection retained only for compatibility, not as the preparation target;
- existing MSVC `/W4 /WX` build contract retained.

The scripts were source-inspected in the current Linux qualification environment. Native Windows installation/build execution of this new bootstrap path is **Platform-unverified** here and must be run on Windows before declaring the dependency bootstrap Windows-verified.
