# Such v1.3.0 Public Handoff

## Repository role

GitHub-safe public frontend: UI, CLI, query syntax, installers, stable C runtime ABI, and `RuntimeClient`. The production search/storage implementation remains private.

## Supported targets

- Windows x64
- Linux x64
- iPadOS source path remains separate from desktop support
- **macOS desktop: unsupported and intentionally removed**

Do not reintroduce a macOS desktop build/runtime path by default.

## Production runtime boundary

- Windows: `SuchRuntimePrivate.dll`
- Linux: `libSuchRuntimePrivate.so`

`SUCH_RUNTIME_LIBRARY` overrides discovery for development/testing. ABI version: 1. Release builds verify pinned SHA-256 values before packaging. The approved v1.3.0 Windows/Linux runtimes export `such_runtime_search_content_v1`, so `/inside` is available in the packaged desktop build.

`SuchMCP` is an optional on-demand integration fetched from its separate public repository. It is not a required Such release artifact. Font bundles are optional.

## Search root commands

- Windows `/index` adds a root; `/drive` replaces all roots.
- Linux `//index` adds a root; `//drive` replaces all roots.
- An empty desktop search field exposes the drive command as the default command hint.

## Publication rule

Publish this tree itself. Never create the public repository by copying the private runtime tree and relying on ignore rules.

## v1.1 Secure Search

- Single search field remains the sole top input surface.
- `Security` toggle sits directly below it.
- No Enterprise activation => fail-closed notice directing users to https://such.heritage-labs.net.
- Heritage Secure Core is temporarily removed from this build; the Security toggle remains fail-closed and shows the Enterprise contact notice only.
- Windows install root is `%LOCALAPPDATA%\\Programs\\Such` for the current user. Automatic AppData indexing remains blocked except for this install directory; `%APPDATA%` and `%LOCALAPPDATA%` are explicit opt-in aliases.

## Root/index architecture

- `RuntimeClient` is the raw ABI adapter only.
- GUI frontends are read/search clients for indexing: they must not call `IndexRootService`, `reindex_async`, `add_root`, or `replace_roots` directly.
- `IndexWorkerLauncher` starts sibling `SuchCLI --index-worker` as a one-shot background process for `/drive`, `/index`, startup rebuild, and explicit reindex.
- `IndexRootService` runs inside the worker and owns root alias resolution, fixed path policy, root mutations, rollback, and deferred noise cleanup.
- startup decides whether indexing is needed before first paint but launches the worker only after the window is visible/mapped.
- `/drive` and `/index` must never recursively enumerate the selected storage on the UI thread.
- noise cleanup runs in the worker after indexing becomes idle and uses bounded runtime-index queries.
- architecture authority: `docs/ARCHITECTURE.md`.

## Build-tool baseline

Keep the approved v1.1.7 root-architecture script structure. v1.3.0 advances release identifiers and the narrow Windows qualification/uninstall/wrapper fixes. Runtime validation consumes `runtime/RUNTIME_SHA256.txt`. Windows uninstall also owns an explicit legacy-machine allowlist for historical v1.1.x installs (`C:\Heritage\Such`, HKLM App Paths/Uninstall, Common Start Menu/Desktop shortcuts, and the old Machine PATH entry); elevation is requested only when one of those artifacts is detected.


## v1.3.0 Korean/IME search

Windows Korean search is a frontend/CLI encoding concern, not a private-runtime limitation. The bundled runtime was verified to match Korean UTF-8 filenames. Keep Win32 IME composition under native EDIT ownership and preserve the wide-argv -> UTF-8 CLI boundary. Do not reintroduce `main(int,char**)` as the Windows CLI entry path.

## Linux ordinary-host build bootstrap

The v1.3.0 Linux build entry points no longer assume that the compiler/CMake/X11/libpng toolchain is preinstalled. `scripts/linux_build_common.sh` owns prerequisite capability probing and package-manager bootstrap. `build_linux.sh`, `verify_linux.sh`, `build_deb.sh`, and `release_linux.sh` select the corresponding dependency profile before resolving their required tools. Prepared hosts do not invoke the package manager. Debian/Ubuntu is the qualified automatic bootstrap/package path; dnf/yum, zypper, and pacman mappings remain platform-unverified until exercised on native hosts.

## Linux finalization notes (2026-09-25)

- X11 Korean/CJK input now follows the XIM ownership path: locale/modifiers -> `XOpenIM` -> supported input-style negotiation -> `XCreateIC` -> `XNFilterEvents` -> `XFilterEvent` -> `Xutf8LookupString`. UTF-8 commit overflow is retried with a dynamic buffer; if a printable composition commit arrives together with Enter, the commit is preserved and that Enter is not reused as a Such command.
- Linux no longer installs HOME as a root on first launch. With no valid persisted roots, the GUI opens the built-in `//drive` picker and waits for explicit selection.
- The former `could not detach the Such index worker` failure was caused by the X11 frontend globally ignoring `SIGCHLD` while the launcher expected to reap a child. Linux now uses the same tracked ownership boundary as Windows: runtime unload -> tracked worker -> poll -> runtime reload.
- `RuntimeClient::unload()` is an explicit frontend/worker ownership primitive and is covered by the ABI smoke test.
- The normal Linux ZIP installation is per-user only. `install_linux.sh` rejects root/sudo and writes below the user's XDG data root and `~/.local/bin`. `build_deb.sh` is a separate optional packaging path that produces a system-managed Debian package without services or autostart entries.
## Linux Samba/CIFS search roots

- Such consumes already-authenticated OS mounts; it does not own Samba credentials or invoke privilege escalation.
- Kernel CIFS/SMB3 mounts are discovered from `/proc/self/mountinfo`. GNOME/GVfs `smb://` shares are surfaced from the current user's `$XDG_RUNTIME_DIR/gvfs/smb-share:*` children.
- `index_root_accessible()` validates candidate, persisted, and explicit roots with the current process credentials before they enter the worker-owned indexing path.
- Permission qualification runs the access gate and production runtime under an unprivileged identity: readable share content must index/search successfully, while an inaccessible subtree must not appear in results.
- Native live-network mounting against a real Samba server remains platform-environment verification; the qualification container does not provide `mount.cifs` or `smbd`.


## v1.3.0 Index Snapshot

Large-disk reuse is implemented as an opaque transport boundary. Public Such must not parse or rewrite private TopoDB/content-index files. `such snapshot export`, `snapshot info`, and `snapshot import` copy persistent runtime state with root/generation metadata and per-file CRC validation. Transient lock/tmp/new files are excluded. Import requires every recorded root to exist at the same absolute path and does not trigger a crawl. Production Linux qualification proved export -> fresh-state import -> immediate search with the original generation and `indexing=no`.

Root remapping and subtree delta reconciliation are intentionally deferred until the private runtime publishes an explicit ABI for them. Do not implement those features by patching opaque index records in Public Such. Linux default state resolution is qualified; Windows snapshot transport requires explicit `SUCH_STATE_DIR` until the private runtime exposes authoritative state-location discovery.

## v1.3.0 Windows public workflow contract

Atomic PowerShell steps remain single-responsibility, but public entry points must be independently runnable. `verify_windows` composes audit -> bootstrap -> build -> test. `install_windows` continues through stage -> install, and `release_windows` continues through stage -> release. Users must never be required to manually create `verified.json` or `staged.json`; those manifests are internal SHA-attested handoffs created by the public workflow. Direct invocation of an atomic downstream step remains fail-closed if its attestation is absent or stale.


## Windows PowerShell host baseline (2026-09-26 hotfix)

All Windows `.cmd` workflows now delegate to `windows_powershell_host.cmd`. The default workflow host is pinned PowerShell 7.6.6 LTS, provisioned as a per-user portable runtime after SHA-256 verification using `windows_powershell_assets.txt`. Windows PowerShell 5.1 remains only as the first-run bootstrap bridge. Installed uninstall support copies the same host shim/asset authority and the uninstall registry entry invokes `uninstall_windows.cmd`, so install and uninstall share one PowerShell-host contract. Linux and iPadOS script paths are unchanged.
