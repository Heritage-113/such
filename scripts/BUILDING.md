# Building Such Public

The script layer follows the authoritative ownership model in `docs/SCRIPT_ARCHITECTURE.md`.

`VERSION` at repository root is the release authority. CMake and platform scripts read that file instead of carrying independent active-version constants.

## Windows

Public workflows:

```bat
scripts\build_windows.cmd -Config Release -Arch x64 -Clean
scripts\verify_windows.cmd -Config Release -Arch x64 -RequireRuntime -Clean
scripts\install_windows.cmd -Config Release -Arch x64 -RequireRuntime
scripts\release_windows.cmd -Config Release -Arch x64 -Clean
scripts\qualify_windows.cmd -Config Release -Arch x64 -RequireRuntime -Clean
```

The public PowerShell entries contain orchestration only. `windows_pipeline.ps1` owns workflow composition; `windows_step_*` scripts are atomic.

Canonical ownership:

```text
bootstrap  dependency preparation
build      CMake configure + MSVC build
 test      CTest + ABI/runtime smoke + verified.json
stage      SHA-verified copy only; no compiler/CMake/CTest
install    current-user install from staged.json
release    ZIP packaging from staged.json
```

Generated Windows state is outside the checkout under `%USERPROFILE%\SW` by default. Toolchain state is under `%USERPROFILE%\STC`. Long source paths may be reached through the short workspace junction. `.cmd` launchers use the central `windows_powershell_host.cmd` utility, which defaults every Windows workflow to the pinned PowerShell 7.6.6 LTS portable host. Windows PowerShell 5.1 is used only as the first-run bridge that downloads/extracts the SHA-verified portable host when it is not already cached. No administrator PowerShell installation is required. When a .cmd entry point is double-clicked, the final result remains visible; CI and scripted invocations return the exit code without waiting. Override using SUCH_KEEP_OPEN=1 or SUCH_KEEP_OPEN=0.

For disconnected use, place the architecture-appropriate official PowerShell 7.6.6 ZIP in `third_party/windows/bootstrap`, beside the installed launcher helpers, or in `%USERPROFILE%\STC\offline` (or `SUCH_WINDOWS_OFFLINE_ROOT`) before using `-OfflineOnly`.

For disconnected builds, prepare the official Visual Studio offline layout on a connected Windows machine with:

```bat
scripts\prepare_windows_offline_toolchain.cmd
```

Then use `-OfflineOnly` on build/verify/install/release as applicable.

### Windows setup EXE

For the standalone Windows x64 installer (Inno Setup 6), run:

```bat
scripts\build_installer_windows.cmd -Config Release -Arch x64
```

This always re-verifies the latest sources and bundles the approved Fix5 DLL. It validates the generated GUI/CLI PE version fields, then creates `dist/installers/Such_v<version>_Windows_x64_Setup.exe`. Generated setup executables are ignored by Git. Inno Setup's ISCC compiler is an external Windows build dependency, not included in SourceOnly. Details: `installer/README_INSTALLER.md`.

## Linux

Public workflows:

```bash
./scripts/build_linux.sh --clean --require-runtime
./scripts/verify_linux.sh --clean --require-runtime
./scripts/qualify_linux.sh --clean --require-runtime
./scripts/install_linux.sh --clean
./scripts/release_linux.sh --clean
./scripts/build_deb.sh --clean
```

Linux generated build/stage/state data lives outside the checkout under `$XDG_CACHE_HOME/Heritage/SuchBuild` (or `$HOME/.cache/Heritage/SuchBuild`). Root-host qualification uses `/tmp/Heritage/SuchBuild` so unprivileged permission fixtures can execute the build outputs.

The build sandbox HOME is separate from the real user HOME. The source installer restores the captured host HOME/XDG values before changing the current-user installation.

Canonical ownership:

```text
linux_step_build.sh        configure + compile only
linux_step_test.sh         CTest-equivalent/runtime/UI/search regression and verified.json
linux_step_stage.sh        copy only exact verified GUI/CLI/runtime
linux_step_deploy_test.sh  per-user install/uninstall regression
linux_step_release.sh      package staged payload only
linux_step_deb.sh          build DEB from staged payload only
linux_payload_installer.sh atomic per-user payload installer
```

On Debian 13 amd64, missing build dependencies may be supplied from `third_party/linux/offline/debian13-amd64`. Other supported package-manager bootstraps are handled only by the bootstrap boundary.

## iPadOS

Public workflows:

```bash
./scripts/build_ipados.sh --simulator --clean
./scripts/verify_ipados.sh --simulator --clean
./scripts/qualify_ipados.sh --simulator --clean
```

Use `--device` for an iPhoneOS SDK build.

Generated Xcode state lives under `~/Library/Caches/Heritage/SuchBuild` by default, not under the source checkout.

Canonical ownership:

```text
ipados_step_bootstrap.sh  macOS/Xcode/CMake/SDK checks only
ipados_step_build.sh      configure + build such_ipados only
ipados_step_test.sh       Such.app / Info.plist / version / app-tree hash
ipados_step_stage.sh      copy only the verified Such.app tree
```

There is deliberately no desktop-style iPadOS install script. Device/App Store deployment is outside the public source build boundary.

## Runtime artifacts

Approved private runtimes remain outside the Public implementation boundary and are consumed through the stable runtime ABI. The pinned runtime hashes live in the version-independent `runtime/RUNTIME_SHA256.txt`; Public `VERSION` does not rename private runtime artifacts.

Strict release verification requires the production runtime where the platform release contract requires it.
