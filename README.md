# Such

**Such, a fast local file search.**

Such is a native local-search tool by Heritage Inc. Desktop support is **Windows x64 and Linux x64**. macOS desktop is not supported.

Current public source snapshot: **v1.3.5**. Product version is read from the single top-level `VERSION` file; the Private Runtime is independently versioned.

https://such.heritage-labs.net

## v1.3.5

- The product version is centrally defined by `VERSION` and used for Windows/Linux titles, Windows class identity, release package names and installation metadata.
- Windows double-click `.cmd` workflows keep the result visible. CI and scripted runs preserve exit codes without waiting.
- Private Runtime Fix5 is still independently versioned and bundled unchanged; the pinned runtime hashes are in `runtime/RUNTIME_SHA256.txt`.
- Windows installer definition (`installer/Such.iss`) and build entry point (`scripts/build_installer_windows.cmd`) produce a per-user setup EXE in ignored `dist/installers/` after Windows MSVC verification. GUI/CLI PE file/product version metadata is generated from `VERSION`.

## v1.3.0 (previous source baseline)

### Index Snapshot

v1.3.0 can carry an existing Linux index to another Such state directory instead of rebuilding a large HDD/NAS index from zero. Public Such treats the private runtime state as opaque bytes; it does not parse TopoDB/content-index internals. Snapshot export waits for the runtime to become idle, closes the runtime handle, excludes transient lock/tmp files, and writes a streamed `.sidx` package with per-file CRC validation.

```text
such snapshot export archive.sidx
such snapshot info archive.sidx
such snapshot import archive.sidx
```

Import does **not** start a crawl. The original absolute search-root paths recorded in the snapshot must still exist. A drive mounted at the same path can therefore become searchable immediately after import. Root remapping and delta reconciliation are deliberately deferred until the private runtime exposes an explicit ABI for those operations; Public Such does not rewrite private index records. Linux default state resolution (`$XDG_DATA_HOME/such/index`, or `~/.local/share/such/index`) is qualified. Windows snapshot transport currently requires an explicit `SUCH_STATE_DIR`.

### Windows workflow repair

The internal Windows step split is retained, but the **public** Windows entry points are independently runnable again. Users no longer need to manually create `verified.json` before running install. Atomic implementations remain separated (`bootstrap`, `build`, `test`, `stage`, `install`, `release`), while the public workflows compose them in order:

- `build_windows` → bootstrap → build
- `verify_windows` → audit → bootstrap → build → test
- `install_windows` → audit → bootstrap → build → test → stage → install
- `release_windows` → audit → bootstrap → build → test → stage → release
- `qualify_windows` → audit → bootstrap → build → test

`verify_windows.cmd -Clean -RequireRuntime` and `install_windows.cmd -Clean -RequireRuntime` are valid again. Stage/install/release still consume SHA-attested artifacts, and the release ZIP is compressed inside the short external Windows workspace before it is copied back to `dist/artifacts`, avoiding long-checkout-path failures. Empty documentation-only font-bundle directories are no longer treated as installable font payloads.

### Existing v1.2.x hardening retained

- Disk crawling remains outside the GUI in one-shot `SuchCLI --index-worker` processes.
- Linux XIM/XIC Korean/UTF-8 input, first-run root selection, CIFS/SMB3 and GVfs share discovery, current-user permission gates, and denied-subtree skipping remain enabled.
- Windows uses the native Unicode EDIT/IME path and the wide-command-line UTF-8 CLI boundary.
- Runtime discovery is restricted to an explicit override or the approved sibling runtime.
- Such remains on-demand only: no Windows service/startup task and no systemd/XDG autostart daemon.
- Source archives retain Windows-safe case-insensitive path, basename, and file-vs-directory collision gates.

## CLI

Windows uses `SuchCLI`; Linux uses `such`.

```text
such report.pdf          search
such root                show roots
such root ~/Work         replace root
such root + /mnt/archive add root
such scan                rebuild index
such status              index status
such snapshot export <file.sidx>  carry current index
such snapshot import <file.sidx>  restore without full crawl
such snapshot info <file.sidx>    inspect snapshot
such pin <file>          pin
such unpin <file>        unpin
such hide <file>         exclude
such show <file>         include
such stop                stop Such processes
such stop --force        force stop
such version             version
such help                help
```

Search needs no command:

```text
such structural drawing
such /dwg plan
such /20260901-20260919 invoice
such /inside warranty
such projectA /; /inside renderer
```

The GUI supports `/;` detail search and `/claude` / `/codex` agent launch. `/inside` switches the query to indexed file contents. When `/inside` follows `/;`, the previous file-result set is passed to the private runtime as candidate file IDs instead of searching the whole corpus.

`SuchMCP` is fetched and built on demand. It is not a required desktop release artifact.

The empty desktop search field also shows the root-selection command directly: `/drive` on Windows and `//drive` on Linux. This replaces the active search root with the selected folder.

## Execution model

Such is an **on-demand application**, not a resident service.

The Windows and Linux build/install/release paths do not register persistent background startup mechanisms such as:

- Windows Run/RunOnce entries
- Startup-folder launchers
- scheduled tasks
- Windows services
- detached resident PowerShell/background services
- systemd units
- XDG autostart entries

A one-shot `SuchCLI --index-worker` child is the only background indexing process. It is launched on demand, owns crawl-producing root/reindex work, waits for the crawl and bounded policy maintenance to finish, then exits. On Windows it is created with `CREATE_NO_WINDOW`; on Linux it is detached from the X11 GUI. It is not a service and has no autostart registration.

Runtime loading is kept separate from index-policy mutation. Existing-index cleanup is not run synchronously from `RuntimeClient::ensure_ready()` during normal startup, and the GUI source is gated against direct crawl-producing root/reindex calls.

## Indexing status

The desktop UI displays **Indexing...** in a status row directly below the search field while an indexing worker is active. The former Security Search control and its Enterprise activation notice have been removed.

On Windows, Such installs for the current user under `%LOCALAPPDATA%\Programs\Such`. AppData remains excluded from automatic indexing. The Such install directory is the only automatic LocalAppData carve-out; sibling applications remain excluded. Entering `%APPDATA%` or `%LOCALAPPDATA%` exactly in the Windows search field and pressing Enter is an explicit user override for that known-folder root. Common noise subtrees such as `node_modules`, `.git`, and `__pycache__` remain excluded even below the Such install directory.

## Benchmark

Controlled synthetic-file benchmark on:

- CPU: Intel Xeon Platinum 8573C
- logical CPUs visible: 5
- RAM: 5.8 GiB
- kernel: Linux 6.18.44 x86_64
- glibc: 2.41
- filesystem: overlayfs, with a second 100k-file run on tmpfs
- baseline: GNU `find` 4.10.0
- benchmark runtime SHA-256: `76f3080395e9352a3fc1840ff221e3325d0c5cf93cfc30c0809cd2bbfa3a0a6a`

The table below compares a **warm loaded Such runtime** against warm-cache recursive GNU `find`. It is not a cold-start comparison.

| Files | Query | Results | Such median | GNU find median | Ratio |
|---:|:---|---:|---:|---:|---:|
| 500,000 | exact hit | 1 | 0.281 ms | 304.29 ms | 1082x |
| 500,000 | miss | 0 | 0.244 ms | 341.98 ms | 1404x |
| 500,000 | broad text | 21,740 | 21.91 ms | 417.43 ms | 19.1x |
| 500,000 | `/dwg` | 125,000 | 100.54 ms | 268.08 ms | 2.7x |
| 500,000 | date filter | 125,000 | 124.28 ms | 752.38 ms | 6.1x |
| 500,000 | text + ext + date | 1,359 | 1.94 ms | 347.39 ms | 179x |

All six query classes passed complete result-set correctness checks against deterministic ground truth: exact hit, miss, broad text, extension, date, and combined filters.

### Runtime load cost

The same 500k-file state occupied about **108.6 MiB**. Sequentially reading those state files took about **12–16 ms**, while `such_runtime_load_v1` took roughly **560–950 ms** depending on CPU affinity.

The same pattern persisted on tmpfs: at 100k files, raw state read was about **2 ms**, while runtime reload was about **100 ms**.

That gap shows that Linux startup cost is dominated by work after raw I/O. The current evidence does not isolate the exact private-runtime function responsible. Native profiling is required before changing the storage format or adding a resident service.

Full benchmark methodology and performance notes are in [`docs/LINUX_PERFORMANCE.md`](docs/LINUX_PERFORMANCE.md).

## Build

Linux:

```bash
./scripts/build_linux.sh --clean
```

The Linux build is intentionally environment-sealed. Run it from any working directory; the script resolves the repository root from its own location and ignores caller-provided build/output/compiler/runtime overrides.

Windows:

```bat
scripts\doctor_windows.cmd
scripts\build_windows.cmd -Clean
scripts\verify_windows.cmd -RequireRuntime
```

The Windows pipeline keeps atomic implementation steps, but every public workflow is independently runnable. `bootstrap_windows` owns toolchain preparation; `windows_step_build` owns configure/build; `windows_step_test` owns CTest/runtime smoke; `windows_step_stage` is toolchain-free and copies SHA-attested artifacts; install and release consume only staged artifacts. `verify_windows` composes audit/bootstrap/build/test, `install_windows` continues through stage/install, and `release_windows` continues through stage/package. `doctor_windows` remains diagnostic-only. The authoritative compiler is Visual Studio 2022/MSVC with `/W4 /WX`; portable CMake 3.31.12 remains SHA-256 pinned. Every public Windows `.cmd` workflow runs through the central PowerShell host shim. It pins PowerShell 7.6.6 as the default portable workflow host, verifies the official archive SHA-256 before materialization, and uses Windows PowerShell 5.1 only as the first-run bootstrap bridge when the pinned host is not already cached.

For disconnected Windows hosts, create the official Visual Studio Build Tools network layout on a connected Windows machine first:

```bat
scripts\prepare_windows_offline_toolchain.cmd
```

The Visual Studio layout is intentionally kept **outside the repository** because its deeply nested package paths can exceed Windows/MSBuild/FileTracker path limits when combined with a long checkout path. The default offline root is `%USERPROFILE%\STC\offline`, with the VS layout under `%USERPROFILE%\STC\offline\VS17`; `SUCH_WINDOWS_OFFLINE_ROOT` or `SUCH_VS_OFFLINE_LAYOUT` can override it (for example `C:\S\TC`). For a fully disconnected first run, place the architecture-appropriate official PowerShell 7.6.6 ZIP in the same offline root before copying it to the disconnected machine, then run `scripts\bootstrap_windows.cmd -OfflineOnly`. The installer uses `--noWeb`. Windows build/stage/verify state is likewise kept outside the source tree under `%USERPROFILE%\SW` by default, so `install_windows` and `verify_windows` do not inherit the checkout's path depth. Native Win32/MSVC execution of this bootstrap path must still be qualified on a Windows host.

## Validation

The v1.3.0 public line is checked with strict-warning builds, the public contract test suite, repository-layout auditing, runtime hash gates, and Linux real-runtime smoke/search tests. The current review baseline also covers load/search/root-list ABI failure paths, quoted roots, date/detail filters, CAD/BIM extension queries, first-run search-folder selection, stale-root recovery through the tracked worker boundary, explicit-drive preservation, and per-user Linux install/uninstall, CIFS/GVfs share discovery, current-user share access gating, and permission-denied subtree skipping.

Platform-specific gates remain separate: Windows requires the native `verify_windows.cmd` pass, and the iPadOS frontend remains a prototype surface rather than a promoted desktop-equivalent release target.

## Linux build dependencies and per-user release

On **Debian 13 amd64**, the closed-network dependency set is bundled under `third_party/linux/offline/debian13-amd64/`: 204 pinned `.deb` packages plus a local APT index and SHA-256 manifest cover build and strict verification. If a required capability is missing on that qualified target, the build tooling uses the bundled `file://` repository first and does not load the host's configured network mirrors. `scripts/bootstrap_linux_offline.sh` may also be run explicitly before entering a closed network.

Other Linux distributions keep the package-manager fallback (`dnf`/`yum`, `zypper`, or `pacman`) when a capability is missing. Those branches do not consume the Debian 13 binary bundle and remain platform-unverified until exercised on their native hosts.

The normal Such Linux release remains **per-user**, while an optional Debian/Ubuntu `.deb` package is also available for administrators who want a system-managed installation. Build the per-user release or DEB with:

```bash
./scripts/build_linux.sh --clean --require-runtime
./scripts/verify_linux.sh --clean --require-runtime
./scripts/release_linux.sh
```

The release ZIP contains `install_linux.sh`, `uninstall_linux.sh`, and a `payload/` tree. Run the installer as the ordinary desktop user, never through `sudo`. Application binaries live under the current user's XDG data directory (`~/.local/share/Heritage/Such` by default) with user command links under `~/.local/bin`. No Such application file is installed under `/usr`, `/opt`, `/etc`, or another system prefix.

On first Linux GUI launch, Such does not silently index HOME. It opens the built-in `//drive` search-folder picker and waits for an explicit user selection.

## Release runtime

The public repository carries the approved desktop runtime artifacts used by the packaged Windows/Linux builds:

```text
runtime/
  windows-x64/SuchRuntimePrivate.dll
  linux-x64/libSuchRuntimePrivate.so
```

Linux builds use a sealed build context: repository-relative build/install directories, a fixed system tool search set, cleared compiler/linker/CMake override variables, and the approved runtime at `runtime/linux-x64/libSuchRuntimePrivate.so`. Caller-provided build environment variables do not redirect the build. Approved runtime hashes are pinned in `runtime/RUNTIME_SHA256.txt`. The bundled Windows/Linux Fix5 runtimes export the optional ABI-v1 content-search entry point used by `/inside`.

The production runtime binaries under `runtime/` are separate Heritage Inc. artifacts and are not covered by the public Apache-2.0 source license.

## SDK and commercial integration

The **Such SDK is private** and is not distributed through this public repository. For native embedding, private runtime integration, or commercial deployment/support, contact Heritage Inc. through:

https://heritage-labs.net

The public repository exposes the stable public boundary required by Such itself. Publication of that boundary does not publish the private SDK or production search/storage implementation.

## Open-source license

The public Such source code, documentation, and public build tooling are licensed under the **Apache License, Version 2.0**. See [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).

The production runtime binaries under `runtime/` and the private Such SDK are outside the public Apache-2.0 grant.

Contributions are governed by [`CONTRIBUTING.md`](CONTRIBUTING.md). Security issues should be reported according to [`SECURITY.md`](SECURITY.md). The public/private component boundary is documented in [`docs/PUBLIC_BOUNDARY.md`](docs/PUBLIC_BOUNDARY.md).

**2026 Heritage Inc.**
