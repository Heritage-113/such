# Such

**Such, a great search tool.**

Very fast local file search by Heritage Inc.

Desktop support: **Windows x64 and Linux x64**. macOS desktop is not supported.

## CLI

```text
such report.pdf          search
such root                show roots
such root ~/Work         replace root
such root + /mnt/archive add root
such scan                rebuild index
such status              index status
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

The GUI also supports `/;` detail search and `/claude` / `/codex` agent launch.

## v1.0

The v1.0 package contains the public Such source plus the required prebuilt production runtime artifacts for Windows x64 and Linux x64. No macOS desktop runtime is shipped.

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

**2026 Heritage Inc.**
