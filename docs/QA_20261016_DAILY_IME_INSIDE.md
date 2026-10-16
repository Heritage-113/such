# Such v1.3 / Fix5 — Daily Refresh + IME + Inside Search (2026-10-16)

## Status: Linux candidate — NOT a qualified Windows/Private release

**Baseline**: user-supplied `Such_v1.3.0_Fix5_IME_Stage2_SearchPolicy_RuntimeUnified_20261015_RuntimeBundled.zip` (397 ZIP entries; archive integrity PASS). Public repository main remains `e26e104a227b6f4e4c1576382e30d6bafca5f72d` as checked through GitHub on 2026-10-16. No repository commit or Actions run was made.

## Ownership and changes

- `src/runtime/DailyIndexRefresh.cpp`, `include/such/runtime/DailyIndexRefresh.h`: one shared, clock-testable GUI refresh schedule; 24h from last recorded completion, 1h retry throttle, persistent UTC state, `*.new` then atomic rename/replacement, malformed file retained. Path is per-user OS state storage; no service, startup task or daemon registration.
- `platform/windows/Win32Frontend.cpp`, `platform/linux/X11Frontend.cpp`: reuse tracked `IndexWorkerLauncher` flow (release loaded runtime before launching, reload afterwards); only schedule when non-demo normal GUI is running, roots are selected and no indexing is running. Manual GUI reindex also updates the timestamp. A Windows worker-launch failure must never look like a successful completion.
- `src/cli/CliApp.cpp`: check worker settles into a readable idle state; CLI `scan` writes completion timestamp as a best-effort compatibility path.
- `src/runtime/SearchableFiles.cpp`, `include/such/runtime/SearchableFiles.h`, `src/runtime/RuntimeClient.cpp`: reject PDF, Office, binary and other known non-text results from `/inside`, including intermediate `/;` candidates before handing IDs to Private ABI; do not change general filename-search admission policy. Text safety verification and protected path checks before Private scanning still require a Private source patch.
- `tests/daily_index_refresh.cpp`, `tests/public_contracts.cpp`, `tests/runtime_client_smoke.cpp`, `tests/fake_runtime.cpp`: pure clock/persistence, content type rejection, candidate and existing ABI smoke. Existing Stage2 IME gate and Hangul syllable composition code is preserved without modification.
- `CMakeLists.txt`: register shared scheduler and regression test.

## Verification outcomes

| Check | Result | Evidence / limitation |
|---|---|---|
| ZIP CRC | PASS | Python ZipFile.testzip returned None, 397 inputs |
| Windows runtime sha256 | PASS | `2b919dfc20a3a0de9d0b5738a172a785d26b138b573034d7f1f477494f835bbb` |
| Linux runtime sha256 | PASS | `cd3dbd1cd1d62f4695c88d56c090134676f06c89e1a79ed2bbdb1a2a0c073552` |
| Linux GCC 14 Release strict warnings | PASS | `-Wall -Wextra -Wpedantic -Wshadow -Werror`, C++20; built GUI, CLI, tests |
| Linux Clang Release strict warnings | PASS | Clang build, GUI, CLI, tests |
| GCC + Clang CTest | PASS | 14/14 each; includes `such_daily_index_refresh` new test |
| X11 --smoke real runtime | PASS | Xvfb with approved original Fix5 SO, isolated state |
| Linux live GUI scheduled refresh | PASS with reservation | Xvfb launched normal GUI, produced UTC state after worker finished; does not independently prove Private commit success |
| Real Fix5 content policy fixture | PASS for fixture | TXT/CPP returned; PDF/DOCX not in `/inside`; PDF filename still searchable; `/;` narrowed to selected TXT |
| Symbolic link escape fixture | PASS for fixture | root-contained symlink to external text did not yield searchable hit |
| 24h no-duplicate / 1h retry / restart | PASS (unit tests) | injected UTC values; corrupt stored state preserved |
| Private successful commit receipt | UNVERIFIED | ABI v1 has no commit acknowledgment; worker exit + reload does not prove durable generation publication |
| Private pre-crawl text-extension and system-root restrictions | NOT IMPLEMENTED | Fix5 Private source not included in archive; Public only filters candidate/result surfaces |
| Windows MSVC /W4 /WX | UNVERIFIED | no Windows/MSVC environment available |
| Native Windows Hangul IME and Linux XIM interactive | UNVERIFIED | existing Stage2 unit tests, X11 smoke do not simulate actual IME composition |
| 10k/100k performance, memory and NAS/offline root | UNVERIFIED | fixture is too small; no representative corpora supplied |
| GitHub commit and Actions | NOT PERFORMED | do not overwrite unqualified v1.1.7 main |

## Important behavioral limitations

1. Daily timestamp storage is currently a **Public-owned compatibility implementation**, not Private Runtime transactional metadata. The runtime's unchanged ABI v1 exposes no reliable commit receipt. `generation` can remain fixed after reindex. The timestamp is provisional, based on exit=0, state reload and idle status. This is not equivalent to a verifiable Private transaction success. Do **not** ship this as a fully compliant release until the Private Fix5 source implements a durable completion receipt or equivalent transaction-proven update.
2. `/inside` file admissibility at the Public boundary is an extension-based conservative guard, not a full content-sniffing/security guarantee; unidentified but safe UTF-8 text suffixes may be omitted. Private must enforce the exact indexed-root set, existing protected exclusions, `indexed=true`, safe UTF-8 text identification and symlink resolution **before scanning**. The current Public file-name search remains unchanged.
3. If a fully normal GUI runs after an upgrade with no daily state file, a refresh is due immediately; there is no reliable historical last-success timestamp in ABI v1. This can cause one extra refresh after upgrading. Successful reindex through the GUI and CLI `scan` writes the timestamp; other CLI root operations are not yet wired.
4. The GUI index worker is a finite detached process, not an installed service. Closing the GUI during an existing worker may allow that worker to finish; lifecycle guarantees across abrupt termination are not established.
5. The packaged Private runtime DLL and SO are **unchanged reference binaries**. No Fix5 Private Runtime source is being distributed or patched. Keep runtime license notices with the binaries.

## Next release gate

Acquire authoritative `SuchRuntimePrivate_v0.8.0_QAConsistencyFix5_WindowsC4389Fix_SourceOnly.zip`. Implement/verify Private durable successful-refresh timestamp and indexed-only text candidate prefilter without changing ABI v1, then qualify the Windows MSVC/IME runtime, interactive Linux XIM, loss-of-root recovery, 10k/100k corpora and regressions. Keep GitHub main untouched until both platform runs pass.
