# Such v1.1.7 Public Code Review

## Scope

Review baseline: the public Such tree promoted from the v0.6.2 DriveIndexSingleScanFix line to v1.1.7.

Reviewed surfaces:

- public runtime ABI/client and dynamic-library discovery
- search/query compatibility layer, date/detail/extension syntax
- Linux mounted-root discovery and native icon path
- `/claude` and `/codex` agent launcher / SuchMCP bootstrap
- frontend singleton lease and local state handling
- CMake exports, strict-warning policy, install/package metadata
- public-repository boundary and runtime-injection rules
- public ABI stub and contract tests

The private search/storage implementation is not part of this source review. The supplied Linux production runtime is exercised as a black box by the release verification suite.

## Fixed before v1.1.7

### Critical

1. **RuntimeClient could become half-loaded.** A runtime handle could exist after `such_runtime_load_v1` failed, making availability checks misleading. Runtime load is now transactional: any bind/ABI/create/load failure tears the handle and library down, and `available()` is true only after a complete successful load.
3. **Runtime search errors were indistinguishable from zero matches.** `RuntimeClient::search` now propagates ABI failure separately; CLI returns an error and desktop frontends render a search-error state.

### High

5. **Failed root enumeration could look like an empty root set.** Root-list ABI failures now propagate through `RuntimeClient::roots(error)`. Startup recovery refuses destructive root repair when enumeration itself failed. CLI and desktop root views surface the error.
6. **Root mutation caused duplicate full-drive scans.** The production runtime already starts its asynchronous crawl from `add_root` / `replace_roots`; frontends no longer immediately issue a second `reindex_async`. Explicit reindex remains available only through the dedicated command/recovery path.
7. **Runtime discovery could fall through to an unintended library search path.** Production loading now uses only `SUCH_RUNTIME_LIBRARY` or the exact runtime sibling of the executable. No bare-soname fallback remains.
8. **Linux startup repaired multiple stale roots with repeated mutations.** Recovery now computes the desired root set first and applies one `replace_roots` mutation, preventing N asynchronous crawls during startup.
9. **Agent commands could mutate global developer tooling.** `/claude` and `/codex` may bootstrap the public SuchMCP bridge, but Such no longer auto-installs or upgrades the Claude/Codex CLI itself. The user-requested agent must already exist.
10. **Agent bootstrap rebuilt SuchMCP unnecessarily.** A successfully built MCP executable is cached and reused; source checkout/build is only needed when the sibling/cached executable is absent.

### Medium / robustness

11. **Linux native icon decode/XImage assumptions were fragile.** PNG decode uses libpng's simplified API, dimensions/allocation are bounded, XImage stride comes from Xlib, and pixel packing honors the actual Visual RGB masks.
12. **Quoted root paths were not normalized.** `//drive "/media/My Drive"` and `//index '/srv/Project Files'` now preserve the intended path without literal quote characters.
13. **Asynchronous indexing UI refresh was incomplete.** Desktop frontends track both runtime generation and indexed-file count so results/progress can update while a crawl is still active.
14. **Pin/index mutation failures were ignored.** Desktop frontends now report failed runtime mutations instead of silently redrawing stale state.
15. **POSIX frontend lock fallback used a predictable shared `/tmp` file.** The fallback now creates/verifies a mode-0700 per-user directory, rejects unsafe ownership/modes, uses `O_NOFOLLOW` where available, and verifies the lock file owner/type.
16. **Version metadata had multiple drift points.** Product version is generated from CMake (`1.1.7`), desktop titles consume the generated version header, the Windows class identity is v1, iPadOS bundle metadata remains separate, and stale v0.6.x strings are rejected by review checks.
18. **Public ABI failure coverage was too narrow.** Stub regressions now cover load failure, search failure, and root-list failure in addition to the normal ABI smoke path.

## v1 search/UX contracts covered by tests

- DWG and the expanded CAD/BIM/document/code extension registry
- `YYYYMMDD~YYYYMMDD`
- `YYYYMMDD-YYYYMMDD`
- `MMDDYYYY-MMDDYYYY`
- bare eight-digit day filter
- `/;` conjunctive detail-search branches
- quoted search-root paths
- `/claude` and `/codex` parsing
- fresh Linux HOME bootstrap
- stale-root replacement
- explicit `//drive` preservation
- production-runtime DWG/date/detail black-box search

## Validation performed

- GCC 14 strict-warning build: PASS
- Clang 17 strict-warning build: PASS
- CTest public suite: PASS (normal ABI + load/search/root-list failure paths)
- Clang ASan + UBSan test run: PASS
- public repository manifest/audit: PASS
- Linux production-runtime SHA-256 gate: PASS
- Linux production-runtime CLI and GUI smoke: PASS
- Linux fresh-install HOME search: PASS
- Linux stale-root recovery/search: PASS
- Linux explicit-drive preservation/search: PASS
- Linux DWG/date/detail real-runtime search: PASS
- Linux manual 2x and automatic 4K GUI smoke: PASS
- installed `SuchPublic` CMake package consumer compile: PASS
- shell/Python script syntax checks: PASS

## Platform gates still requiring their native hosts

- **Windows:** the source contains MSVC `/W4 /WX` policy plus the Windows known-hazard/static audit and runtime PE/SHA gates, but this Linux review host cannot execute MSVC/Win32 GUI code. Run `scripts\\verify_windows.cmd -Config Release -Arch x64 -Clean -RequireRuntime` on Windows for the final native gate.
- **iPadOS:** the current iPad frontend remains a prototype/demo surface rather than a production private-runtime search frontend. It is not promoted to desktop v1 production status by this review.

## Release boundary

The Git-tracked public tree contains no production search/storage implementation and no private runtime binary. Windows/Linux release builders inject the approved runtime from the ignored `.runtime/` staging area, verify its pinned SHA-256, exercise the real runtime, then package it beside the executable. This separation remains an explicit v1 invariant.

## Desktop platform policy
macOS desktop support was removed before final v1.1.7 packaging. Supported desktop targets are Windows x64 and Linux x64.
