# Such

**Such, a fast local file search.**

Such is a native local-search tool by Heritage Inc. Desktop support is **Windows x64 and Linux x64**. macOS desktop is not supported.

https://such.heritage-labs.net

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
```

The GUI also supports `/;` detail search and `/claude` / `/codex` agent launch. `SuchMCP` is fetched/built on demand and is **not** a required release artifact.

## Benchmark

Controlled synthetic-file benchmark on:

- CPU: Intel Xeon Platinum 8573C
- logical CPUs visible: 5
- RAM: 5.8 GiB
- kernel: Linux 6.18.44 x86_64
- glibc: 2.41
- filesystem: overlayfs, with a second 100k-file run on tmpfs
- baseline: GNU `find` 4.10.0
- production runtime SHA-256: `76f3080395e9352a3fc1840ff221e3325d0c5cf93cfc30c0809cd2bbfa3a0a6a`

The table below compares a **warm persistent Such runtime** against warm-cache recursive GNU `find`. It is not a cold-start comparison.

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

That gap is evidence that Linux startup time is dominated by work **after raw I/O**. The current evidence does not identify the exact private-runtime function responsible, so likely causes such as deserialization, allocator pressure, hash/index reconstruction, cache misses, or synchronization contention remain hypotheses until native profiling confirms them. The fact that 5-CPU reload was slower than 1-CPU reload at 500k files also argues against treating this as a simple storage-bandwidth problem.

Full benchmark methodology and performance notes are in [`docs/LINUX_PERFORMANCE.md`](docs/LINUX_PERFORMANCE.md).

## Build

Linux:

```bash
./scripts/build_linux.sh --clean
```

Windows:

```bat
scripts\doctor_windows.cmd
scripts\build_windows.cmd -Clean
scripts\verify_windows.cmd -Clean
```

## Debian / Ubuntu package

On an x86_64 Debian/Ubuntu-family host with the normal build dependencies installed:

```bash
./scripts/build_deb.sh
```

The package is written to:

```text
dist/artifacts/such_1.0.0_amd64.deb
```

The DEB keeps the production runtime beside the real binaries under `/opt/such/bin`, while `/usr/bin/such` and `/usr/bin/SuchCLI` are launch wrappers. This preserves the current runtime-discovery contract without requiring a global `LD_LIBRARY_PATH`.

## Release runtime

The v1.0 repository/release carries the approved desktop runtime artifacts used for production builds:

```text
.runtime/
  windows-x64/SuchRuntimePrivate.dll
  SuchRuntimePrivate_v0.6.2_Linux_x64_RuntimeOnly/libSuchRuntimePrivate.so
```

`build_linux.sh` accepts `RUNTIME_LIBRARY_PATH=/absolute/path/libSuchRuntimePrivate.so` and otherwise discovers the tracked Linux runtime under `.runtime/`. Approved hashes remain pinned in `runtime/RUNTIME_SHA256_v1.0.0.txt`. Linux legal notices are included with the Linux package.

## SDK and commercial integration

The **Such SDK is private** and is not distributed through this public repository. If you need to embed Such search into another native application, integrate the private runtime, or discuss commercial deployment/support, contact Heritage Inc. through:

https://heritage-labs.net

The public repository intentionally exposes only the stable public runtime boundary required by Such itself. Publication of that boundary does not publish the private SDK or production search/storage implementation.

## Open-source license

The public Such source code, documentation, and public build tooling are licensed under the **Apache License, Version 2.0**. See [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).

The production runtime binaries under `.runtime/` are **not licensed under Apache-2.0** and remain separate Heritage Inc. artifacts subject to separate terms. The private Such SDK is likewise outside the public Apache-2.0 grant.

Contributions are governed by [`CONTRIBUTING.md`](CONTRIBUTING.md). Security issues should be reported according to [`SECURITY.md`](SECURITY.md). The public/private component boundary is documented in [`docs/PUBLIC_BOUNDARY.md`](docs/PUBLIC_BOUNDARY.md).

**2026 Heritage Inc.**
