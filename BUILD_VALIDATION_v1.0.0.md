# Such Public v1.0.0 Validation

- Linux GCC strict Release build: PASS
- Linux Clang strict Release build: PASS
- Public CTest suite (contracts, ABI failure paths, CLI): 9/9 PASS
- RuntimeClient dynamic ABI smoke against public test stub: PASS
- Public repository manifest/audit: PASS (105 manifest files)
- Linux production-runtime SHA-256 gate: PASS
- Linux production-runtime CLI + GUI smoke: PASS
- Linux 2x override + automatic 4K GUI smoke: PASS
- Linux fresh HOME bootstrap/search: PASS
- Linux stale-root recovery/search: PASS
- Linux explicit `//drive` preservation/search: PASS
- Linux DWG/date/detail production-runtime search: PASS
- Public tree contains no production search/storage implementation or tracked private runtime binary: PASS




## Linux mounted-drive indexing contract

`add_root` / `replace_roots` are indexing boundaries owned by the runtime. Desktop frontends do not immediately issue a duplicate `reindex_async()` after a successful root mutation. Explicit reindex remains available through the dedicated command/recovery path.

## CLI review

- Shared CLI dispatcher used by `SuchCLI` and Linux `such`: PASS
- CLI CTest suite (`help`, `version`, `status`, search): PASS
- GUI + CLI coexistence under X11: PASS
- Exact-name `SuchMCP` stop black-box test: PASS
- Production runtime `root`, search, pin/unpin, hide/show workflow: PASS
- `stop` bypasses the GUI lease so a stuck/running GUI can always be terminated: PASS

## Desktop platform policy
macOS desktop support was removed before final v1.0.0 packaging. Supported desktop targets are Windows x64 and Linux x64.

## Approved production runtime hashes

- Windows x64 `SuchRuntimePrivate.dll`: `5bbb84adbcafa3b5795cbdbb9fe183eef4bbc92d14e89bce7798a8ea8913d080`
- Linux x64 `libSuchRuntimePrivate.so`: `76f3080395e9352a3fc1840ff221e3325d0c5cf93cfc30c0809cd2bbfa3a0a6a`

These are the only required private desktop artifacts. Linux legal notices are packaged with the release.
