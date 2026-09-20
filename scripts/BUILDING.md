# Building Such public frontend

The public frontend can be built independently, while production packaging requires the approved platform runtime.

## Linux

After cloning from GitHub, the scripts are tracked executable and can be run directly:

```bash
./scripts/build_linux.sh --clean
./scripts/verify_linux.sh --clean --require-runtime
```

`build_linux.sh` accepts `RUNTIME_LIBRARY_PATH=/absolute/path/libSuchRuntimePrivate.so`. If that variable is not set, it discovers the approved Linux runtime under `.runtime/`.

The release ZIP is built with:

```bash
./scripts/release_linux.sh
```

The Debian/Ubuntu package is built with:

```bash
./scripts/build_deb.sh
```

Output:

```text
dist/artifacts/such_1.0.0_amd64.deb
```

The DEB places the real GUI, CLI, and production runtime under `/opt/such/bin` and exposes `/usr/bin/such` plus `/usr/bin/SuchCLI` launch wrappers. Keeping the runtime beside the real executables preserves Such's current runtime-discovery contract without a global loader-path modification.

Normal Linux build dependencies include CMake, a C++20 compiler, X11 development headers, libpng development headers, Python 3, and pkg-config. Strict GUI verification additionally uses `xvfb-run`.

## Windows

```bat
scripts\doctor_windows.cmd
scripts\verify_windows.cmd -Config Release -Arch x64 -Clean
```

The Windows wrappers prefer PowerShell 7 and fall back to Windows PowerShell when required.

## Runtime artifacts

The v1.0 production release uses:

```text
.runtime/
  windows-x64/SuchRuntimePrivate.dll
  SuchRuntimePrivate_v0.6.2_Linux_x64_RuntimeOnly/libSuchRuntimePrivate.so
```

The approved hashes are pinned in `runtime/RUNTIME_SHA256_v1.0.0.txt`.

`SuchMCP` remains optional/on-demand. Font bundles are optional. macOS desktop is unsupported.

## Smoke-test contract

`--smoke` is a product-health contract. A GUI that paints but cannot load the production runtime must not pass strict release verification.
