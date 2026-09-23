# Such v1.1.7 build validation — Secure Core temporarily removed

Date: 2026-09-21

## Scope

This source snapshot deliberately excludes Heritage Secure Core integration after a critical defect was reported in that component.

- no Secure Core binary is bundled
- no Secure Core loader is compiled
- no Secure Core hash manifest is shipped
- build/install/release scripts do not accept or require Secure Core inputs
- the visible Security toggle is fail-closed and only opens the Enterprise contact flow
- the public repository keeps the approved compatibility runtime; `/inside` requires a content-search-capable private runtime (v0.7.0 or newer)

## Linux verification performed here

Environment: Linux x86-64, GCC 14.2, C++20 strict warnings.

- repository layout audit: PASS (111 manifest files)
- configure: PASS
- GUI + CLI strict build: PASS
- Win32/MSVC known-hazard static gate: PASS
- CTest: 9/9 PASS
- production runtime SHA-256 verification: PASS
- Linux GUI/runtime release verification: PASS
- release ZIP creation: PASS
- Debian package creation: PASS
- release ZIP Secure Core entries: 0
- Debian package Secure Core entries: 0
- `/inside warranty` real-runtime regression: PASS
- filename-only false positive exclusion: PASS

## Windows status

Windows native MSVC/PowerShell execution is NOT VERIFIED in this Linux environment. Windows scripts were stripped of all Secure Core parameters/staging paths and the common CMake Win32/MSVC hazard gate passes.
