# Such v1.0.0 Public Windows Build Fix Validation

Date: 2026-09-19

This revision fixes the public split Windows build/verification chain without changing the private runtime ABI.

## Fixed

- removed the stale mandatory `tests/test_v061_frontend.cpp` reference from the Windows audit
- re-enabled and corrected the current Design.md SHA gate
- removed Python as a Windows build prerequisite
- made the test runtime DLL export its ABI symbols on Windows
- removed direct `GetProcAddress` to typed-function-pointer casts that can trigger MSVC C4191 under `/W4 /WX`
- replaced PowerShell-version-sensitive junction inspection with reparse-point checks
- restored the CMake Win32/MSVC known-hazard gate against the current split source tree

## Validation performed here

- GCC 14 strict Release build: PASS
- public CTest: 2/2 PASS
- Linux install staging: PASS
- Clang 17 strict Release: PASS, 2/2 CTest
- Clang ASan+UBSan+leak smoke: PASS, 2/2 CTest
- Design contract SHA gate: PASS
- Win32/MSVC text hazard gate: PASS

A real Windows/MSVC run is still required before claiming native Windows PASS.

## Build-script hardening pass 2

- made `PACKAGE_MANIFEST.txt` the authority for `audit_repository_layout.py`, removing the duplicated allow-list that rejected `BUILD_VALIDATION_v1.0.0_Windows.md`
- added CMake/CTest discovery through PATH, standard Program Files CMake, and Visual Studio's bundled CMake tools
- made the Windows build orchestrator capture each native tool exit code immediately and stop before the next phase
- made GUI smoke verification use a `Start-Process -PassThru` process object plus bounded `WaitForExit`, so `Such.exe` exit status is authoritative across Windows PowerShell 5.1 and PowerShell 7
- made CMD wrappers preserve the PowerShell process exit code and emit an explicit fatal error when neither PowerShell host exists
- retained the short `%LOCALAPPDATA%\SuchBuild\v100` source-junction workspace to protect MSBuild/FileTracker from long source paths

- isolated each source checkout into a short `v100-<source-id>` workspace to prevent CMake-cache/junction collisions without reintroducing long paths
- paired CTest with the resolved CMake installation when possible and bounded the GUI smoke so a broken GUI message loop cannot hang verification indefinitely

## Build-script hardening pass 3 — runtime injection into public release artifacts

- proprietary runtimes are no longer treated as a separate "private build" product; they are release inputs for the public frontend build
- the GitHub-safe source tree keeps runtimes out of `PACKAGE_MANIFEST.txt` and ignores the local `.runtime/` injection area
- Windows x64 release input: `.runtime/windows-x64/SuchRuntimePrivate.dll`
- Linux x64 release input: `.runtime/linux-x64/libSuchRuntimePrivate.so`
- `build_windows.ps1` stages the DLL beside `Such.exe` / `SuchCLI.exe` and into the install staging `bin` directory
- `build_linux.sh` stages the `.so` beside the Linux executables and into the install staging `bin` directory
- `-RequireRuntime` on Windows and `--require-runtime` on Linux turn a missing production runtime into a hard release-build failure
- explicit CI override remains available through `-RuntimeLibraryPath` on Windows and `RUNTIME_LIBRARY_PATH` on Linux
- Windows runtime PE architecture and Linux ELF64/x86-64 architecture are checked before staging
- public contract tests continue to use the purpose-built ABI stub, so source-only development remains possible without proprietary runtime files

Validated Linux public release output contains:

```text
bin/SuchCLI
bin/such
bin/libSuchRuntimePrivate.so
```

The staged Linux runtime SHA-256 matches the supplied authoritative runtime exactly:

`8c39ff76c810845c0056de94447dd65683ccfa63d85af1c7dd8240f81860db0e`

The authoritative Windows x64 runtime input SHA-256 is:

`088f4f344cb0c67ec67c614d1691aa3037bbf0f54826b57dd02704bb15f53654`

A real Windows/MSVC release run is still required before claiming native Windows runtime-staging PASS in this environment.

## PowerShell parser recovery (2026-09-20)

A Windows-only parser regression was found in `scripts/windows_paths.ps1`: diagnostics used an expandable string shaped like `$actual:`, which PowerShell parses as an invalid drive-qualified variable reference. Because Windows entrypoints dot-sourced the helper before auditing scripts, the shared parse error stopped `doctor`, `build`, and `verify` at the same line.

The v1.0.0 recovery changes are:

- use `-f` formatting for runtime architecture/SHA diagnostics; no `$name:` interpolation;
- run `audit_windows.ps1` before dot-sourcing `windows_paths.ps1` in doctor/build/verify/install/release;
- parse every `scripts/*.ps1` file with the native PowerShell parser before CMake;
- keep generator auto-detection and stale CMake cache recovery;
- preserve the v1.0.0 no-fill command bar and CLI behavior.

Expected Windows preflight:

```bat
scripts\doctor_windows.cmd
scripts\build_windows.cmd -Clean
scripts\verify_windows.cmd -Clean
```

## MSVC compile recovery pass (2026-09-20)

A real Windows/MSVC build reached C++ compilation and exposed three `/W4 /WX` portability failures that GCC/Clang had not rejected under the previous warning set:

- `SearchDialect.cpp`: C4456 local-name shadowing across chained `if` initializers;
- `AgentLauncher.cpp`: C2668 ambiguity between `ps_quote(std::wstring_view)` and `ps_quote(std::filesystem::path)` when forwarding `path.wstring()`;
- `tests/fake_runtime.cpp`: C4996 because `std::getenv` is deprecated by the MSVC CRT and warnings are errors.

The recovery changes are:

- give date-range temporaries semantic, unique names instead of reusing `range`;
- explicitly materialize a `std::wstring` and forward it as `std::wstring_view` in the PowerShell quoting helper;
- use `_dupenv_s` in the Windows fake-runtime test path while retaining `std::getenv` on POSIX;
- add `-Wshadow` to GCC/Clang strict builds so the non-MSVC strict-warning gates catch this class earlier;
- extend `CheckWindowsCompileHazards.cmake` to reject the exact stale C4456/C2668/C4996 patterns that caused this Windows failure.

Validation performed after the patch in this environment:

- GCC 14 strict Release build with `-Wshadow`: PASS
- GCC CTest: 9/9 PASS
- Clang strict Release build with `-Wshadow`: PASS
- Clang CTest: 9/9 PASS
- Win32/MSVC known-hazard configure gate: PASS
- public repository manifest audit: PASS (119 files)

A new native MSVC run is still required to claim Windows native PASS beyond the compiler errors reported above.
