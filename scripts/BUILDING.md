# Building Such public frontend

The public frontend can be built independently, while production packaging requires the approved platform runtime.

## Linux

After cloning from GitHub, the scripts are tracked executable and can be run directly:

```bash
./scripts/build_linux.sh --clean
./scripts/verify_linux.sh --clean --require-runtime
```

Linux build scripts do not consume caller-provided `CONFIG`, build-directory, compiler, linker, runtime, package-version, architecture, artifact-directory, or temporary-directory overrides. They derive the repository root from the script location, use fixed repository-local build/install paths, sanitize the tool search environment, and load the tracked compatibility runtime only from `.runtime/linux-x64/libSuchRuntimePrivate.so`.

The build still requires the normal system packages (CMake, `make`, a C++20 compiler, X11/libpng development headers, Python 3, binutils and standard Debian/Ubuntu packaging tools where applicable), but user shell configuration does not select or redirect them.

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
dist/artifacts/such_1.1.7_amd64.deb
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

The public v1.1 source tree can build against the approved compatibility runtime tracked in `.runtime/`. `/inside` requires a content-search-capable private runtime (v0.7.0 or newer), supplied separately when that optional extension is needed:

```text
.runtime/
  windows-x64/SuchRuntimePrivate.dll
  linux-x64/libSuchRuntimePrivate.so
```

The approved hashes are pinned in `runtime/RUNTIME_SHA256_v1.1.7.txt`.

`SuchMCP` remains optional/on-demand. Font bundles are optional. macOS desktop is unsupported.

## Smoke-test contract

`--smoke` is a product-health contract. A GUI that paints but cannot load the production runtime must not pass strict release verification.

## Secure Search provider

Heritage Secure Core is intentionally excluded from this build. The Security toggle remains visible but fail-closed and routes to Enterprise contact only. No Secure Core library is loaded, staged, packaged, or required.
## Process lifetime policy

Such is an on-demand desktop/CLI application. Installation and release scripts must not register Windows Run/RunOnce entries, Startup-folder launchers, scheduled tasks, Windows services, systemd units, or XDG autostart entries. Verification treats a detached GUI process remaining after smoke termination as a release failure.

