# Building Such public frontend

The public frontend source builds without the proprietary runtime. A distributable **release artifact does not**: release packaging requires the approved platform runtime and verifies it before packaging.

## Frontend-only development

Linux:

```bash
scripts/verify_linux.sh --clean
```

Windows:

```bat
scripts\doctor_windows.cmd
scripts\verify_windows.cmd -Config Release -Arch x64 -Clean
```

These paths build the public UI/CLI/contracts and verify the stable ABI with `SuchRuntimeStub`. They are intentionally valid without the private production engine.

## Runtime injection for public release artifacts

The runtime is never tracked by Git. Put the approved binary in the git-ignored local injection area:

```text
.runtime/
  windows-x64/
    SuchRuntimePrivate.dll
  linux-x64/
    libSuchRuntimePrivate.so
```

The exact approved v1.0.0 hashes are pinned in `runtime/RUNTIME_SHA256_v1.0.0.txt`. Release scripts reject an absent, wrong-architecture, or wrong-hash runtime. These two runtime binaries are the only private build artifacts required by the desktop public release. `SuchMCP` is optional/on-demand and font bundles are optional. macOS desktop is unsupported.

### Windows x64 release

```bat
scripts\release_windows.cmd -Clean
```

This performs configure/build/CTest/install, ABI-stub CLI+GUI smoke, **production-runtime CLI+GUI smoke**, then creates:

```text
dist/artifacts/Such_v1.0.0_Windows_x64_Public.zip
```

The ZIP contains `SuchRuntimePrivate.dll` beside `Such.exe` and `SuchCLI.exe`. Windows v1.0.0 production runtime is x64-only.

### Linux x64 release

```bash
scripts/release_linux.sh
```

This performs configure/build/CTest/install, ABI-stub CLI+GUI smoke, **production-runtime CLI+GUI smoke**, verifies the staged runtime byte-for-byte, then creates:

```text
dist/artifacts/Such_v1.0.0_Linux_x64_Public.zip
```

The ZIP contains `libSuchRuntimePrivate.so` beside `such` and `SuchCLI`. Linux runtime legal notices are packaged under `share/doc/such/runtime/`. Strict release GUI smoke requires `xvfb-run`.

## Smoke-test contract

`--smoke` is a product-health contract. The GUI now returns a non-zero exit code when its runtime cannot be loaded, so a window that merely paints without the production engine can no longer pass release CI.

## Windows build notes

The Windows wrappers prefer PowerShell 7 and fall back to the built-in Windows PowerShell 5.1 by absolute path. The CMake Visual Studio generator is auto-detected instead of hard-coded. Incompatible stale CMake caches are deleted automatically. Short source junctions are used only when the source path is long enough to risk MSBuild/FileTracker path-length failures. `SUCH_BUILD_ROOT` can override the workspace root.
