# Such Public Architecture

## 1. Scope

This document defines the responsibility boundaries of the public Such desktop codebase.

Supported desktop targets:

- Windows x64
- Linux x64

The public repository contains the frontend, CLI, policy layer, build/install tooling, and the stable runtime ABI adapter. The private search/storage implementation remains behind `SuchRuntimePrivate`.

The main rule is simple:

> A user action must not trigger an unbounded filesystem walk on the UI thread.

This rule is especially important for `/drive`, `/index`, startup recovery, and explicit reindex operations.

---

## 2. Layer ownership

### 2.1 Frontends

Files:

- `platform/windows/Win32Frontend.cpp`
- `platform/linux/X11Frontend.cpp`

Responsibilities:

- collect keyboard/mouse input
- open platform-native folder pickers
- show command suggestions and errors
- draw indexing/search state
- poll runtime status on the existing UI refresh cadence
- launch the sibling `SuchCLI` index worker for crawl-producing root/index operations

Frontends must not:

- recursively enumerate a search root
- implement AppData/system-path policy
- call private runtime ABI functions directly
- call `IndexRootService`, `RuntimeClient::reindex_async()`, `RuntimeClient::add_root()`, or `RuntimeClient::replace_roots()` directly
- duplicate root-policy rules between Windows and Linux

---

### 2.2 Command parser

Files:

- `include/such/ui/IndexCommands.h`
- `src/ui/IndexCommands.cpp`

Responsibilities:

- parse `/drive`, `/index`, `/reindex`, `/roots` on Windows
- parse `//drive`, `//index`, `//reindex`, `//roots` on Linux
- preserve quoted paths
- recognize `%APPDATA%` and `%LOCALAPPDATA%` as explicit Windows-only root commands

This layer describes user intent only. It does not mutate the runtime.

---

### 2.3 Index worker process

Files:

- `include/such/runtime/IndexWorkerLauncher.h`
- `src/runtime/IndexWorkerLauncher.cpp`
- hidden CLI worker path in `src/cli/CliApp.cpp`

The GUI process does not own disk crawling. Any operation that can start a full or partial crawl is handed to a sibling `SuchCLI` process through the hidden `--index-worker` protocol.

Responsibilities:

1. serialize the requested operation as `reindex`, `add`, or `replace`
2. encode root paths as UTF-8 hex so Windows non-ASCII paths do not pass through the ANSI command-line boundary
3. launch `SuchCLI` without a console on Windows (`CREATE_NO_WINDOW`)
4. launch a detached one-shot child on Linux
5. keep the worker alive until runtime indexing becomes idle
6. drain bounded `IndexRootService` policy maintenance in the worker
7. exit when the requested indexing transaction is complete

The worker is **not a resident service**. It exists only while an explicit/startup indexing transaction is active.

Important invariant:

- the Win32/X11 GUI source must contain no direct `IndexRootService` use and no direct `reindex_async`, `add_root`, or `replace_roots` call
- startup may decide that a rebuild is needed before the window appears, but the worker is launched only after the first window is visible/mapped

---

### 2.4 IndexRootService

Files:

- `include/such/runtime/IndexRootService.h`
- `src/runtime/IndexRootService.cpp`

This is the application-service boundary for root changes.

Responsibilities:

1. resolve root aliases
2. ask `IndexPathPolicy` for bounded fixed exclusions
3. apply those exclusions
4. call the raw `RuntimeClient` root mutation
5. mark deferred policy maintenance pending
6. after runtime indexing becomes idle, scrub noise subtrees through bounded runtime-index queries

Important invariant:

- `plan_default_index_policy()` defaults to bounded/non-recursive planning
- `/drive` and `/index` never call `plan_default_index_policy(..., true)` on the user-interaction path
- no root mutation recursively walks the selected drive before returning

Noise maintenance is performed after the private runtime crawl and uses the runtime index in bounded batches. The current maintenance terms are:

- `node_modules`
- `.git`
- `__pycache__`

Each maintenance query is capped at 512 result rows, and at most 16 new subtree exclusions are applied per UI tick. Discovered prefixes are remembered so a runtime that temporarily returns already-excluded rows cannot trap the maintenance state machine in a loop. The same term is queried again on later ticks until no additional prefixes remain.

This avoids both failure modes of the old implementation:

- UI freeze caused by a synchronous public filesystem pre-scan
- two concurrent full-drive filesystem crawls competing for the same storage

---

### 2.5 IndexPathPolicy

Files:

- `include/such/runtime/IndexPathPolicy.h`
- `src/runtime/IndexPathPolicy.cpp`

Responsibilities:

- classify protected Windows paths
- resolve `%APPDATA%` and `%LOCALAPPDATA%`
- define fixed OS/application-data exclusions
- define the Such installation carve-out
- identify default noise directory names
- derive a noise-directory prefix from an indexed result path
- optionally perform explicit filesystem discovery for tests/offline tooling

The policy layer may contain filesystem traversal code, but product root mutation paths must not invoke recursive discovery synchronously.

---

### 2.6 RuntimeClient

Files:

- `include/such/runtime/RuntimeClient.h`
- `src/runtime/RuntimeClient.cpp`

`RuntimeClient` is a raw ABI adapter.

Responsibilities:

- find and load the runtime library
- bind ABI v1 symbols
- create/load/destroy the runtime handle
- translate public C++ calls to ABI calls
- surface ABI errors
- expose search, status, root mutation, pin/index mutation, and engine statistics

RuntimeClient must not:

- decide which user paths are allowed
- interpret `%APPDATA%` or `%LOCALAPPDATA%`
- recursively enumerate roots
- own `/drive` policy
- mutate index policy during `ensure_ready()`

The separation exists so runtime loading stays deterministic and startup cannot accidentally perform migration or filesystem work before the window appears.

---

## 3. `/drive` execution flow

### Windows

1. user types `/drive` or selects the folder picker
2. `IndexCommands` returns `ReplaceRoot`
3. Win32 frontend obtains the selected filesystem path
4. frontend calls `launch_index_worker(ReplaceRoots, root)`
5. `SuchCLI.exe --index-worker replace ...` starts with `CREATE_NO_WINDOW`
6. the worker loads `RuntimeClient` and constructs `IndexRootService`
7. `IndexRootService` compiles bounded root-set policy only
8. the service snapshots the previous roots for rollback
9. the worker resets runtime roots, seeds fixed exclusions, then adds the prepared roots
10. private runtime performs the crawl in the worker process, not the GUI process
11. the GUI remains responsive and observes generation/status changes through its read/search runtime handle
12. after indexing becomes idle, the worker drains bounded noise maintenance and exits

### Linux

The same ownership model applies, with `//drive` as the command syntax. The X11 frontend releases its `RuntimeClient` before starting a **tracked** sibling `SuchCLI --index-worker`, polls the worker ticket from the event loop, and reloads a fresh runtime only after the worker exits. The frontend must not globally ignore `SIGCHLD`, because tracked worker completion relies on `waitpid`.

A fresh Linux install has no implicit root. The frontend enters the built-in `//drive` picker after the first window is created and waits for an explicit user choice. HOME is offered as a candidate, not adopted automatically. If all persisted roots are stale, a tracked worker clears the stale root set and the frontend returns to the same picker.

Linux network shares follow the OS mount boundary. `LinuxIndexRoots` recognizes kernel `cifs`/`smb3` mountpoints from `/proc/self/mountinfo` and immediate GVfs `smb-share:*` children under the current user's `$XDG_RUNTIME_DIR/gvfs`. The picker never asks for or stores Samba credentials. A root is offered only if it can be opened with the current process credentials. The same access check is used when validating persisted roots and explicit `//drive` selections. Such does not elevate, impersonate another user, or bypass Samba/Unix ACLs; inaccessible roots are rejected and inaccessible subtrees remain the private runtime crawler's skip boundary.

---

## 4. Reindex flow

`/reindex` is an explicit rebuild request.

The public path is:

1. GUI recognizes the explicit rebuild request
2. GUI launches the sibling one-shot index worker and returns to the event loop immediately
3. worker loads the persisted root set
4. worker reconstructs any explicit AppData override intent
5. worker compiles bounded root-set policy
6. worker reseeds the runtime as root reset -> exclusions -> roots
7. private runtime rebuilds inside the worker process
8. after indexing becomes idle, worker performs bounded noise maintenance and exits

A public recursive filesystem pre-scan must not be inserted before the private runtime crawl, and a crawl-producing runtime mutation must not be reintroduced into the GUI process.

---

## 5. Windows installation model

Such is installed for the current user only.

Install root:

```text
%LOCALAPPDATA%\Programs\Such
```

Current-user ownership means:

- no UAC elevation
- no Administrator PowerShell
- no HKLM writes
- no machine PATH mutation
- no CommonPrograms/CommonDesktop shortcut ownership

Registration locations:

- App Paths: `HKCU\Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe`
- uninstall: `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\HeritageSuch`
- PATH: current-user PATH only
- Start Menu: current-user Programs folder
- Desktop shortcut: current-user Desktop only when requested

The Windows installer workflow runs under the pinned PowerShell 7.6.6 LTS host. Only the minimal host-provisioning bridge remains runnable under Windows PowerShell 5.1.

---


## 5A. Linux installation model

Such is installed for the current Linux user only. `scripts/install_linux.sh` rejects root/sudo execution. The default application root is:

```text
$XDG_DATA_HOME/Heritage/Such
# default: ~/.local/share/Heritage/Such
```

The real GUI, CLI, and private runtime remain sibling files under the per-user application root so executable-relative runtime discovery remains valid. `~/.local/bin/such` and `~/.local/bin/SuchCLI` are user-owned links. The desktop entry and icon are written below the current user's XDG data directory. The default Linux release does not use system-wide application paths. Separately, the optional DEB packaging path installs the managed payload under `/opt/such` with `/usr/bin` launch wrappers because dpkg installation is system-wide by design. The DEB packaging boundary does not add a daemon, systemd unit, or autostart entry.

---
## 6. AppData policy and the Such carve-out

Automatic indexing excludes AppData by default.

Explicit search-box aliases remain available:

```text
%APPDATA%
%LOCALAPPDATA%
```

Those aliases are explicit user intent and may opt the corresponding current-user known folder into indexing.

There is one automatic carve-out:

```text
%LOCALAPPDATA%\Programs\Such
```

The Such install directory and its descendants are allowed even though they live under LocalAppData.

The carve-out does not open the rest of AppData. Sibling LocalAppData and `Programs` applications remain excluded. Default noise rules still apply below the Such directory, so a nested `node_modules`, `.git`, or `__pycache__` remains excludable.

Other users' AppData trees remain excluded as complete subtrees.

---

## 7. `/inside` boundary

`/inside` is a public query mode backed by an optional ABI-v1 content-search export:

```text
such_runtime_search_content_v1
```

Ordinary filename/path search can work with a compatibility runtime that does not export that symbol. `/inside` requires a content-search-capable private runtime.

Release/runtime manifests must therefore match the binary actually shipped. A source release that advertises `/inside` but packages an older compatibility runtime is invalid.

---

## 8. Startup model

Such is on-demand and non-resident.

Forbidden product behavior:

- Run/RunOnce registration
- Startup-folder launchers
- scheduled tasks
- Windows services
- systemd units
- XDG autostart
- detached **resident** helper processes

`RuntimeClient::ensure_ready()` loads the runtime only. It does not run index migration or policy cleanup. A one-shot `SuchCLI --index-worker` child is allowed only for an active indexing transaction and must exit after the crawl/maintenance completes.

---

## 9. Cross-platform naming rule

Identifiers must not be distinguished only by letter case.

Do not create pairs such as:

```text
path / PATH
root / Root
config / CONFIG
```

Use semantic names instead:

```text
file_path
runtime_path
search_root
install_root
build_config
```

This rule applies to C++, PowerShell, shell scripts, build variables, and future Such platform code.

---

## Script control-plane ownership

Build, verification, staging, installation, release, and qualification script ownership is defined authoritatively in `docs/SCRIPT_ARCHITECTURE.md`. Platform scripts must follow that document rather than duplicating workflow semantics in local wrappers. Product architecture remains defined here; script/control-plane architecture remains defined there.

---

## 10. Validation gates

A root/indexing change is not complete until the following pass where applicable:

- public repository layout audit
- strict C++ build
- CTest public contract suite
- runtime ABI smoke tests
- `/inside` real-runtime regression
- explicit `/drive` preservation regression
- stale-root recovery regression
- Windows static source audit
- Windows native `verify_windows.cmd` on a Windows host

The Windows source audit additionally guards these architectural invariants:

- `RuntimeClient` must not contain root-policy planning or recursive root traversal
- `IndexRootService` must use bounded policy planning for root mutation
- Win32/X11 GUI frontends must delegate crawl-producing operations to `launch_index_worker()` and must not call root mutation/reindex APIs directly
- current-user install must not request elevation or write HKLM
- Windows PowerShell 5.1-incompatible `ProcessStartInfo.ArgumentList` must not return
- Windows pipeline steps are responsibility-isolated: build cannot test/stage/install, test cannot build/stage/install, stage cannot build/test/install, and install/release cannot invoke build tooling
- `doctor_windows` is observational only and must not bootstrap or create build/install state

### Windows build pipeline ownership

Windows build automation is treated as a small pipeline, not as a collection of mutually recursive scripts. Public entry points orchestrate atomic steps in a fixed order:

```text
build   : audit -> bootstrap -> build
verify  : audit -> bootstrap -> build -> test
install : audit -> bootstrap -> build -> test -> stage -> install
release : audit -> bootstrap -> build -> test -> stage -> release
```

Atomic steps never invoke sibling steps. `windows_native.ps1` owns native-process argument quoting/environment propagation. `windows_paths.ps1` owns repository-local `build/` workspace/toolchain paths. `windows_artifact_state.ps1` owns verification/staging manifests. The test step records SHA-256 hashes of the binaries and optional production runtime in `verified.json`; the stage step refuses modified inputs and emits `staged.json`; install and release refuse unstaged or hash-mismatched payloads. A rebuild invalidates prior attestations. Generated Windows build and attestation state lives under the repository-local `build/` directory; installed payloads remain separate.


## Index Snapshot transport boundary

`IndexSnapshot` owns transport of an already-built runtime state. Public Such treats the private runtime directory as an opaque set of regular files and never parses or rewrites TopoDB/content-index records. Export is permitted only after the runtime reports idle and the CLI releases its runtime handle. Transient `.lock`, `.tmp`, and `.new` files are excluded. The snapshot container stores public transport metadata (format version, absolute root list, generation, payload entries) plus a CRC for every payload file.

Import validates the complete container before replacing the current state, rejects unsafe relative paths and corrupted payloads, and requires every recorded absolute root to exist at the same path. Import does not call `replace_roots`, `add_root`, `reindex_async`, or the external index worker. A successful import therefore preserves the transported generation and becomes searchable immediately. Root remapping/delta reconciliation are **Deferred** until the private runtime owns explicit ABI operations for those semantics; Public Such must not patch private storage records to simulate them.

Linux default runtime-state resolution is `$XDG_DATA_HOME/such/index` or `~/.local/share/such/index` and is **Verified** against the production runtime. `SUCH_STATE_DIR` remains authoritative for test/custom state. Windows default state discovery is **Platform-unverified/Deferred** for snapshots, so v1.3.0 requires explicit `SUCH_STATE_DIR` for Windows snapshot import/export rather than guessing a private-runtime path.

Validation includes the standalone snapshot container contract plus a production-runtime Linux regression that indexes a fixture, exports it, imports it into a fresh state directory, verifies the same generation with `indexing=no`, and searches the transported index without a replacement-root/reindex operation.

## External index worker completion boundary

Filesystem crawling is never owned by the GUI process. On Windows, `/drive`, `/index`, and reindex operations start sibling `SuchCLI --index-worker` with `CREATE_NO_WINDOW`. The frontend retains a process ticket. Its runtime timer polls the ticket; once the worker exits, the frontend discards the stale runtime view, opens a fresh `RuntimeClient`, reloads roots/generation/indexed-file state, and refreshes the current query. A second crawl cannot start while a tracked worker is running.

Drive/folder selection uses `IFileOpenDialog` with `FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST`, with the legacy shell browser only as a fallback. Selecting a whole volume root such as `D:\` is therefore a first-class `/drive` operation.

Windows uninstall removes legacy user state under `%LOCALAPPDATA%\Such` and `%APPDATA%\Such`. It also owns a fixed legacy-machine allowlist for historical v1.1.x installs: `C:\Heritage\Such`, the corresponding HKLM App Paths/Uninstall keys, Common Start Menu/Desktop shortcuts, and `C:\Heritage\Such\cli` in Machine PATH. Normal v1.3.0 uninstall remains current-user-only; elevation is requested only when one of those machine-wide artifacts is detected. Enterprise security state under `%LOCALAPPDATA%\Heritage\Such\Security` and the historical `%ProgramData%\Heritage\Such\Security` remain separately controlled by the explicit purge switch.


### Linux Unicode / IME input boundary

The X11 frontend initializes the process locale and X locale modifiers before opening the input method, creates an `XIC`, merges the `XNFilterEvents` mask into the window event selection, and passes XIM-owned events through `XFilterEvent`. Committed text is read with `Xutf8LookupString`; `XBufferOverflow` is retried with a dynamically sized buffer and only `XLookupChars` / `XLookupBoth` payloads enter the UTF-8 query buffer. Backspace removes one UTF-8 code point. This boundary is intended for Korean/CJK composition through the desktop XIM bridge (for example IBus/Fcitx) without byte-wise corruption or Such pre-empting composition events.

### Windows Unicode input boundary

The desktop search control is a Unicode Win32 `EDIT`. `GetWindowTextW` is converted to UTF-8 before entering `SearchDialect` / `RuntimeClient`. The search subclass must not consume command/navigation keys while an IME composition is active; `WM_IME_STARTCOMPOSITION` establishes IME ownership and `WM_IME_ENDCOMPOSITION` schedules a fresh query after the composition commits. This is an input-ownership rule, not a search-engine normalization rule.

`SuchCLI` follows the same UTF-8 core contract. On Windows the process entry point accepts wide arguments and converts each argument to UTF-8 before calling `such::cli::run`. No search query may cross the Windows ANSI code-page boundary.
