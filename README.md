# Such

**Such, a fast local file search.**

Such is a native local-search tool by Heritage Inc. Desktop support is **Windows x64 and Linux x64**. macOS desktop is not supported.

Current public source snapshot: **v1.1.7**.

https://such.heritage-labs.net

## v1.1.7

This release tightens startup, indexing, desktop UI behavior, and release reproducibility.

- Runtime startup no longer runs existing-index migration from `RuntimeClient::ensure_ready()`. Runtime load and index-policy mutation are separate paths.
- Windows uses the native `EDIT` cue banner for the search placeholder. The previous manual placeholder repaint path is gone.
- Root add/replace operations remain the indexing boundary. Frontends do not immediately trigger a second full reindex after a root mutation.
- Runtime load, search, root enumeration, pin, and index-mutation failures are surfaced instead of being treated as empty or successful states.
- Runtime discovery is restricted to the explicit `SUCH_RUNTIME_LIBRARY` override or the exact runtime beside the executable. There is no bare library-name fallback.
- Linux startup root repair computes the final root set first and performs one replacement instead of repeated mutations.
- Linux text input uses XIM/XIC with UTF-8 lookup when available, including UTF-8-safe deletion behavior.
- Windows DPI/font refresh creates replacement fonts before releasing the old GDI handles used by the search control.
- `/claude` and `/codex` do not install or upgrade the agent CLI. `SuchMCP` is built only when needed and a successful build is reused.
- Linux release builds use a sealed repository-relative build context and ignore caller-provided compiler, build-directory, install-directory, runtime-path, and CMake override variables.
- Such remains on-demand only. No Windows startup entry, service, scheduled task, systemd unit, or XDG autostart entry is installed.

## CLI

Windows uses `SuchCLI`; Linux uses `such`.

```text
such report.pdf          search
such root                show roots
such root ~/Work         replace root
such root + /mnt/archive add root
such scan                rebuild index
such status              index status
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

## Execution model

Such is an **on-demand application**, not a resident service.

The Windows and Linux build/install/release paths are checked so they do not register or ship background startup mechanisms such as:

- Windows Run/RunOnce entries
- Startup-folder launchers
- scheduled tasks
- Windows services
- detached PowerShell background jobs
- systemd units
- XDG autostart entries

GUI smoke verification also checks that a detached Such process is not left behind after the test window exits.

Runtime loading is kept separate from index-policy mutation. Existing-index cleanup is not run synchronously from `RuntimeClient::ensure_ready()` during normal startup.

## Secure Search

The desktop UI exposes a compact **Security** toggle below the search field. Secure Search is fail-closed: ordinary local search remains available, but the toggle does not become active unless the private Heritage Secure Core returns an activated Enterprise security session.

Heritage Secure Core is temporarily excluded from this public v1.1 line. In this build the Security toggle opens the Enterprise contact notice and does not activate Secure Search.

On Windows, Such installs machine-wide under `C:\Heritage\Such`. AppData remains excluded from automatic indexing, but entering `%APPDATA%` or `%LOCALAPPDATA%` exactly in the Windows search field and pressing Enter explicitly opts that known folder into indexing. Common noise subtrees such as `node_modules`, `.git`, and `__pycache__` remain excluded.

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
scripts\verify_windows.cmd -Clean -RequireRuntime
```

The Windows verification path includes the known-hazard/static audit and the pinned runtime PE/SHA checks. Native Win32/MSVC verification still has to run on a Windows host.

## Validation

The v1.1.7 public line is checked with strict-warning builds, the public contract test suite, repository-layout auditing, runtime hash gates, and Linux real-runtime smoke/search tests. The current review baseline also covers load/search/root-list ABI failure paths, quoted roots, date/detail filters, CAD/BIM extension queries, fresh-home bootstrap, stale-root recovery, and explicit-drive preservation.

Platform-specific gates remain separate: Windows requires the native `verify_windows.cmd` pass, and the iPadOS frontend remains a prototype surface rather than a promoted desktop-equivalent release target.

## Debian / Ubuntu package

On an x86_64 Debian/Ubuntu-family host with the normal build dependencies installed:

```bash
./scripts/build_deb.sh
```

Release artifacts are written under `dist/artifacts/`. The DEB keeps the production runtime beside the real binaries under `/opt/such/bin`, while `/usr/bin/such` and `/usr/bin/SuchCLI` are launch wrappers. This preserves runtime discovery without requiring a global `LD_LIBRARY_PATH`.

The v1.1.7 source snapshot uses v1.1.7 build-directory, artifact-name, and runtime-hash manifest identifiers.

## Release runtime

The public repository carries the approved desktop compatibility runtime artifacts used by the packaged Windows/Linux builds:

```text
.runtime/
  windows-x64/SuchRuntimePrivate.dll
  linux-x64/libSuchRuntimePrivate.so
```

Linux builds use a sealed build context: repository-relative build/install directories, a fixed system tool search set, cleared compiler/linker/CMake override variables, and the compatibility runtime at `.runtime/linux-x64/libSuchRuntimePrivate.so`. Caller-provided build environment variables do not redirect the build. Approved compatibility-runtime hashes are pinned in `runtime/RUNTIME_SHA256_v1.1.7.txt`. `/inside` content search is an optional ABI-v1 extension and requires a compatible newer private runtime supplied separately.

The production runtime binaries under `.runtime/` are separate Heritage Inc. artifacts and are not covered by the public Apache-2.0 source license.

## SDK and commercial integration

The **Such SDK is private** and is not distributed through this public repository. For native embedding, private runtime integration, or commercial deployment/support, contact Heritage Inc. through:

https://heritage-labs.net

The public repository exposes the stable public boundary required by Such itself. Publication of that boundary does not publish the private SDK or production search/storage implementation.

## Open-source license

The public Such source code, documentation, and public build tooling are licensed under the **Apache License, Version 2.0**. See [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).

The production runtime binaries under `.runtime/` and the private Such SDK are outside the public Apache-2.0 grant.

Contributions are governed by [`CONTRIBUTING.md`](CONTRIBUTING.md). Security issues should be reported according to [`SECURITY.md`](SECURITY.md). The public/private component boundary is documented in [`docs/PUBLIC_BOUNDARY.md`](docs/PUBLIC_BOUNDARY.md).

**2026 Heritage Inc.**
