# Such v1.3.0 Public Code Review

## Scope

Review baseline: the previous root-architecture/per-user SourceOnly line promoted and requalified as v1.3.0.

Reviewed surfaces:

- public runtime ABI/client and dynamic-library discovery
- root mutation architecture and index policy
- Windows path/Unicode handling
- Win32 frontend and process control
- Linux frontend/root behavior
- CLI/query syntax including `/inside`, `/drive`, and detail search
- Windows PowerShell 5.1 build/install tooling
- CMake/MSVC warning policy and platform link dependencies
- public/private repository boundary
- public contract/runtime-client tests

The private search/storage implementation is not part of this source review. Approved runtime binaries are treated as opaque release inputs and checked by architecture/hash/ABI behavior.

## Critical/high fixes carried into v1.3.0

1. **Root selection/startup indexing could freeze or terminate the GUI.** Even after recursive public pre-scan removal, the private runtime crawl was still started from the GUI process. Win32/X11 now delegate `/drive`, `/index`, startup rebuild, and explicit reindex to a one-shot sibling `SuchCLI --index-worker` process. GUI source contains no direct `IndexRootService`, `reindex_async`, `add_root`, or `replace_roots` crawl path.
2. **Root policy and ABI responsibilities were mixed.** `RuntimeClient` is now a raw ABI adapter. Root aliases, AppData exceptions, exclusion planning, rollback, and deferred noise cleanup belong to `IndexRootService` / `IndexPathPolicy`.
3. **Parent-dominant exclusion semantics could invalidate AppData exceptions.** Full root replacement/reindex now uses root reset -> exclusion seed -> root add. Explicit current-user AppData roots are compiled as global exceptions to broader-root exclusion plans.
4. **`/inside` could silently disappear when an old compatibility runtime was packaged.** v1.3.0 runtime manifests pin content-search-capable binaries and strict release verification performs a real `such_runtime_search_content_v1` query.
5. **Windows native-process launching must remain Windows PowerShell 5.1 compatible.** The responsibility now lives only in `windows_native.ps1`: `ProcessStartInfo.ArgumentList` is forbidden; argument quoting uses `ProcessStartInfo.Arguments`, and environment control uses `EnvironmentVariables`.
6. **Windows installation requested elevation for a personal desktop search tool.** Installation is current-user only under `%LOCALAPPDATA%\Programs\Such`, with HKCU registration and User PATH. No UAC/Admin PowerShell is part of the install path.
7. **`such stop` could match unrelated same-name processes.** Process control now verifies the executable location as well as the basename before requesting close/termination.

## MSVC worker hotfix

The external index-worker launcher was re-audited for `/W4 /WX`. Two implicit character-width/sign conversions introduced with the worker split were removed: range iteration no longer initializes `unsigned char` directly from `char`, and ASCII worker arguments are widened to `wchar_t` with explicit casts rather than a heterogeneous `std::wstring` iterator constructor. The CMake Windows hazard gate now rejects both stale patterns.

## MSVC-specific review findings

The Linux/GCC path could not expose several Windows-only defects. The v1.3.0 audit therefore explicitly reviewed `_WIN32` branches under MSVC rules.

### Wide path type mismatch

Windows `std::filesystem::path` comparison keys are `std::wstring`. A Windows-only branch passed that key into `is_windows_protected_index_path(std::string_view)`, which would not compile under MSVC. Policy classification now crosses an explicit UTF-8 conversion boundary instead.

### Persisted root encoding

The runtime ABI returns UTF-8 strings. Reconstructing a Windows `std::filesystem::path` directly from `std::string` can pass through the active ANSI code page and break Korean/non-ASCII paths. `IndexRootService` now converts UTF-8 to UTF-16 with `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)` first.

### Win32 callback/subclass ABI

- search EDIT subclassing uses `SetWindowSubclass`, `DefSubclassProc`, and `RemoveWindowSubclass`; the old integer/function-pointer WNDPROC cast path is forbidden by the static gate
- `EnumWindows` uses an explicit `BOOL CALLBACK` thunk
- `STARTUPINFOW::cb` and Toolhelp structure sizes use explicit `DWORD` casts
- the Windows shared core receives `UNICODE`, `_UNICODE`, `NOMINMAX`, `_WIN32_WINNT=0x0A00`, and `WINVER=0x0A00`
- Win32 frontend links `comctl32` for subclass helpers

### Build tooling baseline

The script architecture remains the approved v1.1.7 root-architecture baseline. v1.3.0 advances release identifiers and the narrow Windows qualification/uninstall/wrapper fixes. Runtime validation consumes `runtime/RUNTIME_SHA256.txt`.

## AppData/current-user install policy

Automatic AppData indexing remains blocked. The automatic carve-out is the exact current-user Such install subtree:

```text
%LOCALAPPDATA%\Programs\Such
```

Sibling applications remain protected. Exact `%APPDATA%` / `%LOCALAPPDATA%` search-box aliases represent explicit user intent and are handled separately. Default dependency/cache noise rules still apply inside an otherwise allowed root.

## Validation performed on the review host

- repository layout audit: PASS (360 manifest files in the current v1.3.0 tree)
- GCC strict-warning build: PASS
- GCC CTest: 13/13 PASS
- Clang 17 strict-warning build: PASS
- Clang CTest: 13/13 PASS
- Clang ASan + UBSan CTest: 13/13 PASS
- hidden index-worker command smoke: PASS
- detached index-worker launcher smoke: PASS
- CMake frontend ownership gate forbidding in-process GUI crawl calls: PASS
- CMake Win32/MSVC known-hazard configure gate: PASS
- Linux production-runtime SHA-256 gate: PASS
- real-runtime root A -> root B replacement: PASS
- replaced-root persistence: PASS
- old-root leakage check: PASS
- real-runtime `/inside` search: PASS
- X11 Hangul UTF-8 query-boundary driver (`가`, U+AC00): PASS
- production-runtime Korean and mixed Korean/ASCII filename search: PASS
- fresh-install `//drive` picker display, explicit selection, persisted root, and real search hit: PASS
- stale-root tracked-worker recovery and historical detach-error regression: PASS
- Linux per-user root/sudo refusal plus unprivileged install/uninstall: PASS
- full `verify_linux.sh --require-runtime`: PASS

Native IBus/Fcitx composition itself remains platform-unverified on this container because no desktop Korean IME daemon/engine is installed. The X11 UTF-8/XIM boundary and the production search boundary are independently covered by automated tests.

## Native platform gate

Windows source auditing is substantially stronger in v1.3.0, but the exact local v1.3.0 SourceOnly tree has not yet completed a native MSVC build in this environment. The connected GitHub `main` is older than the local root-architecture baseline, so attempts to apply only the v1.3.0 delta to `main` are not a valid native qualification.

Final Windows release qualification must therefore run from the packaged SourceOnly tree:

```bat
scripts\doctor_windows.cmd
scripts\build_windows.cmd -Config Release -Arch x64 -Clean
scripts\verify_windows.cmd -Config Release -Arch x64 -RequireRuntime -Audit
scripts\install_windows.cmd -Config Release -Arch x64 -SkipFonts -RequireRuntime
```

Do not treat this document as a native Windows PASS until those commands succeed on Windows.

## Release boundary

The public repository contains no private search/storage implementation. Approved runtime binaries under `runtime/` are separately licensed artifacts, pinned by SHA-256 and exercised through the public ABI. The Such SDK remains private.

## Desktop platform policy

Supported desktop targets are Windows x64 and Linux x64. macOS desktop support remains intentionally removed. iPadOS remains a separate prototype/source path rather than a promoted desktop-equivalent release target.

## v1.3.0 focused fixes

- `/drive` and other crawl-triggering commands now retain an external-worker ticket instead of fire-and-forget launch.
- The GUI releases its runtime handle before the worker opens persistent state, then reloads a fresh runtime after the worker exits. This fixes the external-process state-lock failure reproduced with the production runtime.
- Windows drive/folder selection uses `IFileOpenDialog` with a legacy shell fallback. Whole-volume roots are supported.
- Win32 search input now tracks `WM_IME_STARTCOMPOSITION` / `WM_IME_ENDCOMPOSITION`. While composition is active, native EDIT/IME handling owns Enter, Escape, Tab, and candidate-navigation keys; Such refreshes the UTF-8 query after commit instead of preempting composition.
- Windows `SuchCLI` uses `wmain` and converts wide arguments with `WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS)` before calling the platform-neutral CLI core.
- UTF-8 Korean query preservation is covered by the public parser contract and fake-runtime ABI smoke, and the bundled Linux production runtime was exercised with Korean filenames (`한글검색테스트.txt`, `회의록_프로젝트.pdf`).
- Windows uninstall removes legacy `%LOCALAPPDATA%\Such` and `%APPDATA%\Such` state directories and the verified machine-wide v1.1.x footprint (`C:\Heritage\Such`, HKLM App Paths/Uninstall, Common Start Menu/Desktop shortcuts, and the old Machine PATH entry). It requests elevation only when those machine-wide artifacts are detected. Enterprise security state remains behind the explicit purge option.

## Linux finalization review

The Linux frontend had three coupled product gaps. First, XIM events were not fully owned by the input method: the window did not negotiate all supported input styles, did not include the XIC filter mask in its selected events, and did not consistently filter IME-owned events before application key handling. The frontend now negotiates a supported XIM style, merges `XNFilterEvents`, calls `XFilterEvent`, reads commits with `Xutf8LookupString`, retries `XBufferOverflow`, deletes UTF-8 by code point, and consumes an Enter event when that same event carries a printable XIM commit.

Second, first launch no longer adopts HOME implicitly. With no valid persisted roots, Linux enters the built-in `//drive` picker and waits for an explicit user choice. Qualification drives the picker with X11 input, verifies the selected root is persisted through the external worker, and confirms a real indexed search hit.

Third, the screenshot-visible `could not detach the Such index worker` error was traced to the frontend globally setting `SIGCHLD` to `SIG_IGN` while `IndexWorkerLauncher` still required `waitpid()` ownership. The global ignore was removed. Linux now follows the established runtime ownership sequence: GUI runtime unload -> tracked external worker -> worker exit -> runtime reload.

Linux application installation is now current-user-only. `install_linux.sh` rejects root/sudo with RC 13 and installs below the user XDG data root plus `~/.local/bin`; `uninstall_linux.sh` removes only those owned application files. The per-user ZIP installer remains non-root; `build_deb.sh` separately produces an optional system-managed Debian package.

## Linux dependency bootstrap review

The Linux prerequisite failure is owned by the build-tooling boundary, not by product sources or CMake targets. `scripts/linux_build_common.sh` now provides one shared capability probe/bootstrap path used by build, strict verification, Debian packaging, and Linux release entry points.

The bootstrap first checks the exact capabilities the scripts/CMake require. Package-manager, network, and elevation operations are skipped when those capabilities are already present. If anything is absent, the build tooling installs the distro package profile and then re-probes the capabilities instead of assuming package installation succeeded.

For Debian 13 amd64, missing-package recovery is now offline-first. The tracked bundle contains the 204-package dependency closure for build, verification, release, and DEB packaging. `bootstrap_linux_offline.sh` verifies the bundle hashes, rejects non-Debian-13/non-amd64 hosts, and creates an isolated APT configuration whose only source is the local `file://` repository. The host's configured mirrors are not loaded by this path. The local bootstrap executed successfully with RC 0; all 204 package archives also passed `dpkg-deb --info`. A synthetic clean-host APT run with an empty dpkg status database and no source other than the bundle resolved all 204 packages and reported `Need to get 0 B/216 MB`, proving that the dependency closure is locally complete for the qualified profile. Other package-manager branches remain source-reviewed but platform-unverified in this environment.
## Linux Samba/CIFS review

Linux network-root support is owned by `platform/linux/LinuxIndexRoots.*`, not by `RuntimeClient` or the private search implementation. Kernel CIFS/SMB3 mounts and per-user GVfs SMB children are converted into ordinary filesystem roots only after a current-credential directory-open check. Persisted roots and explicit picker selections use the same gate. No credential cache, `sudo`, setuid helper, or ACL bypass was added.

The deterministic root contract covers CIFS/GVfs discovery and inaccessible mount exclusion. Strict production qualification additionally lowers execution to an unprivileged identity, indexes a readable fixture through the real runtime, and verifies that a denied child subtree is not searchable. Live remote Samba transport remains platform-unverified because the qualification host has no `mount.cifs`/`smbd`.


## Windows dependency bootstrap

- `bootstrap_windows.ps1` is the owner for Windows build-prerequisite provisioning.
- The product still builds with Visual Studio 2022/MSVC; MinGW/alternate ABI fallback is not introduced.
- CMake 3.31.12 is pinned and SHA-256 verified before extraction.
- Missing MSVC tools are provisioned with `Microsoft.VisualStudio.Workload.VCTools`; the offline path consumes an official Visual Studio network layout with `--noWeb`.
- build_windows owns bootstrap+build; verify/install/release do not bootstrap or rebuild; `doctor_windows.ps1` is diagnostic-only and never provisions the toolchain.
- `.toolchain/` is generated/local state and is excluded from repository-layout authority.
- Native execution of the new dependency bootstrap remains Windows-platform-unverified in the current Linux qualification environment.


## v1.3.0 archive basename invariant

Distributable source files must have globally unique basenames under a case-insensitive comparison, and the repository must not contain case-insensitive file-vs-directory path collisions. This is intentionally stricter than normal Linux directory semantics because Windows extraction targets cannot represent layouts such as a `Packages` file beside a `packages/` directory. The Debian offline payload directory is therefore named `pool/`. Xcode-required `Contents.json` names are generated in the iPadOS build directory from uniquely named source templates rather than stored twice in the source archive.


## Windows pipeline responsibility split

The v1.3.0 Windows automation was refactored to remove mutually recursive build/install/verify responsibilities. Atomic ownership is now:

- `bootstrap_windows.ps1`: toolchain provisioning only;
- `windows_step_build.ps1`: configure/build only;
- `windows_step_test.ps1`: CTest, ABI stub smoke, production-runtime smoke, and `/inside` regression only;
- `windows_step_stage.ps1`: toolchain-free SHA-verified artifact staging only;
- `windows_step_install.ps1`: current-user installation from an attested staged tree only;
- `windows_step_release.ps1`: release ZIP packaging from an attested staged tree only.

`windows_native.ps1` owns native process quoting/environment propagation. `windows_artifact_state.ps1` owns `verified.json` / `staged.json` and SHA-256 handoff validation. A build invalidates prior verification/staging state. Atomic steps never call each other; public verify/install/release workflows compose audit/bootstrap/build/test/stage/install-or-release so they remain independently runnable. Cross-platform repository audit and `audit_windows.ps1` both reject role drift.

Current refactor-host validation: repository audit PASS (360 manifest files), strict Linux build + CTest 13/13 PASS, full Linux qualification PASS, and DEB packaging PASS at version 1.3.0. Native Windows execution remains Platform-unverified until rerun on Windows.


## Index Snapshot review

The v1.3.0 snapshot path is deliberately a transport layer rather than a second index implementation. Public Such records only root/generation metadata and copies private runtime state as opaque files. It does not parse or rewrite TopoDB/content-index records. Export requires the runtime to be idle and closed, omits transient `.lock`/`.tmp`/`.new` files, writes per-entry CRC32, publishes through a temporary file, and removes that temporary output on failure. Import validates the complete stream and root availability before replacing state, rejects symlinks/unsafe relative paths/trailing bytes, stages into a sibling directory, and rolls the previous state back if activation fails.

Qualified behavior on Linux production runtime: export an indexed state, import it into a fresh state directory with the same absolute root, and search immediately without a crawl or generation change. Root remapping and delta reconciliation remain deferred until the private runtime exposes an explicit ABI; Public Such must not patch private index records itself. Windows snapshot default-state discovery remains Platform-unverified, so `SUCH_STATE_DIR` is required there for v1.3.0.
