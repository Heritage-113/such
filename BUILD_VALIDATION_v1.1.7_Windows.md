# Such v1.1.7 Windows validation status — Secure Core temporarily removed

Date: 2026-09-21

## Current Windows release contract

Expected machine-wide payload:

```text
C:\Heritage\Such\
  Such.exe
  SuchCLI.exe
  SuchRuntimePrivate.dll
```

Heritage Secure Core is intentionally NOT part of this build. The Security toggle is fail-closed and routes to the Enterprise contact notice only.

## Script contract

Use:

```bat
scripts\doctor_windows.cmd
scripts\build_windows.cmd -Config Release -Arch x64 -Clean -RequireRuntime
scripts\verify_windows.cmd -Config Release -Arch x64 -Clean -RequireRuntime
```

There is no `-RequireSecureCore` or `-SecureCoreLibraryPath` option in this snapshot.

## Verification status in the current environment

- CMake Win32/MSVC known-hazard gate: PASS
- Linux build of shared public code with strict warnings: PASS
- CTest shared/public contracts: 9/9 PASS
- native Windows MSVC build: NOT VERIFIED HERE
- native Windows PowerShell execution: NOT VERIFIED HERE

Run the commands above on Windows before release qualification.
