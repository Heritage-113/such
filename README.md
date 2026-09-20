# Such

**Such, a fast local file search.**

Such is a native local-search tool by Heritage Inc. Desktop support is **Windows x64 and Linux x64**. macOS desktop is not supported.

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

## Build

Linux:

```bash
scripts/build_linux.sh --clean
```

Windows:

```bat
scripts\doctor_windows.cmd
scripts\build_windows.cmd -Clean
scripts\verify_windows.cmd -Clean
```

## Release runtime

The public repository never tracks the proprietary runtime. Production release builders inject exactly one platform runtime:

```text
.runtime/
  windows-x64/SuchRuntimePrivate.dll
  linux-x64/libSuchRuntimePrivate.so
```

The approved hashes are pinned in `runtime/RUNTIME_SHA256_v1.0.0.txt`. No other private binary is required by the public build. Linux legal notices shipped with the runtime should accompany the Linux release. Font bundles are optional.

**2026 Heritage Inc.**
