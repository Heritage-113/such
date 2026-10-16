# Such v1.3.0 build validation

Date: 2026-09-26

## Release scope

Such v1.3.0 is the public Windows/Linux release line. Heritage Secure Core remains excluded from this public build; the visible Security toggle stays fail-closed and opens the Enterprise contact flow only.

The approved Windows/Linux private runtimes used by this source snapshot export `such_runtime_search_content_v1`, so `/inside` is part of the release qualification rather than a documentation-only capability.

## Architecture changes

- `RuntimeClient` remains a raw ABI adapter and does not own root/index policy.
- GUI frontends no longer execute crawl-producing root/index mutations in-process. `/drive`, `/index`, startup rebuild, and explicit reindex are delegated to a one-shot sibling `SuchCLI --index-worker` process.
- `IndexRootService` remains the owner of root policy, mutation, rollback, and deferred maintenance inside the worker.
- Root replacement is ordered as runtime-root reset -> bounded exclusion seed -> root add. This matches the private runtime's parent-dominant exclusion semantics.
- User-facing root mutations do not recursively enumerate the selected storage on the caller/UI thread.
- Windows AppData remains excluded by default; `%LOCALAPPDATA%\Programs\Such` is the automatic current-user install carve-out.
- Windows persisted runtime root strings are converted from UTF-8 to UTF-16 before constructing `std::filesystem::path`.
- Windows process shutdown validates both the Such process basename and its executable location before termination.

## Linux verification performed

Environment: Debian 13 x86-64 qualification container.

- public repository layout audit: PASS (360 manifest files in the current v1.3.0 tree)
- hermetic GCC strict-warning build: PASS
- GCC CTest: 13/13 PASS
- Clang strict-warning build: PASS
- Clang CTest: 13/13 PASS
- Clang ASan + UBSan build/CTest: 13/13 PASS
- hidden `SuchCLI --index-worker reindex` test against fake runtime: PASS
- tracked index-worker launcher smoke: PASS
- GUI source gate forbidding direct `IndexRootService` / reindex/add/replace crawl calls: PASS
- Win32/MSVC known-hazard configure gate: PASS
- production Linux runtime SHA-256 gate: PASS (`6fe027a5552fce5042438e46eec20f8c884a55ce4b7d598dd990efd7be91ed33`)
- real-runtime `/inside` search: PASS
- X11 UTF-8 Korean input driver (`가`, U+AC00) through XIM -> GUI query buffer -> RuntimeClient boundary: PASS
- production-runtime Korean filename search (`한글`, `한글검색`, `건축`, `구조`, `아주대학교`, `설계`, `문서`) and mixed Korean/ASCII queries: PASS
- opaque Index Snapshot contract (CRC, unsafe-path rejection, transient-file exclusion, same-root enforcement): PASS
- production-runtime Index Snapshot export -> fresh-state import -> immediate search without reindex: PASS
- X11 GUI scaling smoke at explicit 2x and automatic 4K: PASS
- fresh-install first-run root behavior: PASS; no HOME auto-indexing, built-in `//drive` picker entered
- first-run picker interaction (`Down` + `Return`) -> explicit HOME selection -> worker indexing -> persisted root -> real search hit: PASS
- stale-root recovery through RuntimeClient unload -> tracked worker -> runtime reload: PASS
- regression for historical `could not detach the Such index worker`: PASS; failure is no longer emitted
- explicit `/drive` persistence across GUI restart and HOME non-leakage: PASS
- Linux current-user installer root/sudo rejection: PASS (RC 13)
- Linux install/CLI/uninstall under a disposable unprivileged `nobody` HOME: PASS
- full `verify_linux.sh --require-runtime`: PASS
- per-user Linux release ZIP generation: PASS
- extracted release ZIP preserves installer executable bits and installs/uninstalls as an unprivileged user: PASS
- optional Debian package: `build_deb.sh` produces `dist/artifacts/such_1.3.0_amd64.deb`; build is unprivileged, while installation is system-wide by Debian design; no service/autostart payload is permitted

### Closed-network dependency bundle

- Linux dependency capability probe on a prepared Debian 13 host: PASS
- Debian 13 amd64 offline dependency closure: 204 repacked `.deb` packages, 215,815,902 package bytes: PASS
- offline bundle SHA-256 verification: 209 manifest entries PASS (204 packages plus bundle metadata/index files)
- all 204 bundled packages parse with `dpkg-deb --info`: PASS
- isolated local APT bootstrap using only the bundled `file://` repository: PASS, RC 0
- synthetic clean-host APT resolution with an empty dpkg status database and only the local bundle configured: PASS; 204 packages resolved, `Need to get 0 B/216 MB`
- `build_linux.sh --clean --require-runtime` after Windows-pipeline refactor: PASS, CTest 13/13, production runtime staged and hash-verified

The per-user ZIP release and the build-dependency bundle have different privilege models. The ZIP installer must not be run with sudo/root. The optional DEB is a separate system-managed installation format and therefore is installed through the normal Debian package manager. Preparing an otherwise bare closed-network machine with distro development packages can also require administrator privileges because those are operating-system build dependencies, not Such application files.

### Native Korean IME qualification boundary

The automated X11 gate verifies an actual Hangul Unicode code point through the XIM/XIC UTF-8 lookup path and exact RuntimeClient query bytes. The production runtime separately verifies Korean and mixed Korean/ASCII filename matching. The source also negotiates a supported XIM input style, merges `XNFilterEvents`, calls `XFilterEvent`, handles `XBufferOverflow`, and preserves a printable XIM commit returned together with Enter instead of reusing that Enter as a Such command.

A real desktop IBus/Fcitx Hangul composition session is **Platform-unverified in this container** because neither IME daemon/engine is installed here. Final native-machine acceptance should type a composed Korean phrase in the search box under the target Ubuntu/Debian desktop session.

## Windows qualification status

Static/source review remains in place for MSVC and Windows PowerShell 5.1. v1.3.0 now also splits Windows build automation into explicit bootstrap/build/test/stage/install/release ownership boundaries, with SHA-256 manifests between verification and staging. The following Windows-only defects and script-contract issues were found before native qualification:

- Windows `std::filesystem` path key (`std::wstring`) was incorrectly passed to a UTF-8 policy API; fixed with an explicit UTF-8 boundary.
- persisted UTF-8 runtime roots were being reconstructed through the active ANSI code page; fixed with `MultiByteToWideChar(CP_UTF8)`.
- Win32 EDIT subclassing now uses `SetWindowSubclass` / `DefSubclassProc` instead of C4191-prone WNDPROC casting.
- `EnumWindows` now uses an explicit `BOOL CALLBACK` thunk.
- `ProcessStartInfo.ArgumentList` is not used; the build launcher is compatible with Windows PowerShell 5.1.
- shared Windows core compilation explicitly defines `UNICODE`, `_UNICODE`, `NOMINMAX`, `_WIN32_WINNT=0x0A00`, and `WINVER=0x0A00`.
- crawl-producing GUI actions now spawn sibling `SuchCLI.exe` with `CREATE_NO_WINDOW`; the GUI does not call root mutation or reindex APIs directly.
- Win32 search input preserves active IME composition; Such no longer consumes Enter/Escape/navigation keys before Korean composition can commit.
- Windows CLI entry uses a wide command line and explicit UTF-8 conversion instead of inheriting the active ANSI code page through `char** argv`.

The Windows automation ownership contract is additionally checked by both `audit_windows.ps1` and the cross-platform repository audit. Atomic steps remain isolated, while the public `verify`, `install`, and `release` workflows create their own prerequisites in order. Build invalidates prior `verified.json` / `staged.json`; test emits `verified.json`; stage validates it and emits `staged.json`; direct internal install/release steps refuse unattested or hash-mismatched payloads. `doctor_windows.ps1` is diagnostic-only.

A native MSVC run from this environment could not yet qualify the full local v1.3.0 tree because the connected GitHub `main` is older than the local root-architecture SourceOnly baseline. The Windows release is therefore not marked native-verified by this document. Run the Windows qualification commands from this exact SourceOnly tree before promotion.

## Windows commands

```bat
scripts\doctor_windows.cmd
scripts\build_windows.cmd -Config Release -Arch x64 -Clean
scripts\verify_windows.cmd -Config Release -Arch x64 -Clean -RequireRuntime
scripts\install_windows.cmd -Config Release -Arch x64 -Clean -SkipFonts -RequireRuntime
```

Expected install root:

```text
%LOCALAPPDATA%\Programs\Such
```

No elevation is required.

### v1.3.0 external-worker regression

The external-worker lifecycle was additionally exercised against the bundled Linux production runtime, not only the public stub. Keeping a GUI/runtime handle open while a second worker opened the same persistent state reproduced a worker load failure. Releasing that handle before worker launch, waiting for worker completion, and then constructing a fresh runtime handle passed root replacement and state reload. This ordering is now an architectural invariant and is checked by the Windows source audit.

The current script-refactor tree passes GCC strict, CTest 13/13, and the full Linux qualification. Earlier v1.3.0 C++ qualification also passed Clang strict and Clang ASan/UBSan 13/13; this patch changes Windows automation only. Native Windows/MSVC execution remains the final platform qualification gate.
## Linux Samba/CIFS qualification

- synthetic `/proc/self/mountinfo` contract for CIFS discovery/labeling: PASS
- synthetic GVfs `smb-share:*` discovery/labeling: PASS
- current-user root access gate under unprivileged UID: PASS
- production runtime indexing/search of readable share fixture: PASS
- production runtime skip of permission-denied subtree: PASS
- GCC strict + CTest: 13/13 PASS
- Clang 17 strict + CTest: 13/13 PASS
- ASan + UBSan + CTest: 13/13 PASS
- live external Samba server / real `mount.cifs` session: Platform-unverified in this container

The supported ownership model is OS-authenticated mount -> current-user permission check -> existing external index worker. Such does not persist SMB credentials or elevate around filesystem ACLs.


### v1.3.0 DEB packaging regression fix

A prior packaging revision incorrectly replaced `scripts/build_deb.sh` with an unconditional RC 13 refusal. That made the documented Debian packaging profile unreachable even though `linux_build_common.sh` still retained the `deb` dependency profile. The packaging owner has been restored without changing the default per-user ZIP installation contract.

Verified on Debian 13 amd64:

- `./scripts/build_deb.sh`: PASS, RC 0
- public repository audit invoked by the build: PASS (360 manifest files)
- GCC production build and CTest: 13/13 PASS
- production runtime staging/SHA gate: PASS
- generated package: `dist/artifacts/such_1.3.0_amd64.deb`
- `dpkg-deb --info`: PASS; package/version/architecture are `such` / `1.3.0` / `amd64`
- package payload check: PASS; GUI, CLI, sibling private runtime, desktop entry, icon, and runtime notices are present
- resident service/systemd/autostart payload check: PASS (none present)
- extracted-package `SuchCLI version`: PASS (`1.3.0`)
- extracted-package production-runtime root/scan/search smoke: PASS, including Korean filename/query

The per-user ZIP remains the default no-root installer. The DEB is an optional system-managed Debian/Ubuntu format; building it is unprivileged, while installing it uses the normal system package manager.


### v1.3.0 archive-name integrity

The distributable source tree now enforces globally unique, case-insensitive file basenames and rejects case-insensitive file-vs-directory path collisions in `scripts/audit_repository_layout.py`. The Debian offline repository uses `pool/` for `.deb` payloads so its `Packages` index is representable on Windows filesystems. Supporting README files use role-specific names, and iPadOS asset-catalog metadata is stored as uniquely named templates that CMake materializes as `Contents.json` only in the generated build tree. The final source ZIP must pass exact-path, case-insensitive-path, file-vs-directory, and basename collision scans after creation and again after extraction.
