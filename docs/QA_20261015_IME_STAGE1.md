# Such v1.3 / Fix5 — IME stage 1 (2026-10-15)

Status: BENCHMARK / REVIEW CANDIDATE — **NOT A RELEASE**.

This source-only snapshot is based on the user-uploaded `Such_v1.3.0_Fix5_RuntimeSwap_BenchmarkCandidate_20261015(1).zip`. It preserves Public ABI v1 and leaves both Private Fix5 binaries unchanged. GitHub Public `main` remains `e26e104a227b6f4e4c1576382e30d6bafca5f72d`.

## Work performed

- `platform/windows/Win32Frontend.cpp`: suppress searches during Win32 IME preedit; dispatch only changed committed text, including post-IME events, while preserving non-IME edit notifications.
- `platform/linux/X11Frontend.cpp`: retain existing XIM commit-only path; deduplicate identical committed query dispatches.
- `include/such/ui/CommittedInputGate.h`: shared frontend-only composition and duplicate query gate. No UTF-8 byte-count-based length heuristic is used.
- `include/such/ui/HangulQueryNfc.h`, `src/ui/HangulQueryNfc.cpp`, `src/runtime/RuntimeClient.cpp`: algorithmic Hangul Jamo NFC composition for `/inside` search requests; leaves the editor text and ABI untouched. This is **Hangul-only** normalization, not full Unicode NFC.
- `tests/public_contracts.cpp`: preedit/commit/duplicate/delete/cancel/Hangul composition contract tests.

## Evidence

- Linux GCC Release strict build: PASS.
- Public CTest: 13/13 PASS.
- X11 `--smoke` using supplied real Fix5 SO: PASS under Xvfb.
- Real Fix5 SO: `/inside 검색` and the canonically decomposed-Hangul equivalent return the same real TXT hit: PASS.
- Real Fix5 SO: selected TXT and CPP content found, PDF/DOCX and binary omitted, and `/;` + hide/show smoke passed: PASS on a small Linux fixture only.
- Filename and content result equality between original and patched Public CLI, same Fix5 runtime: PASS on test fixture.
- Performance: 30 alternating warmed process-per-query observations; see accompanying `such_ime_perf.json`. Not generalizable to large indexes.
- Windows MSVC `/W4 /WX` and interactive Korean IME: UNVERIFIED.
- Linux interactive XIM/Hangul IME: UNVERIFIED (the X11 `--smoke` does not simulate composition).
- Complete system path, symlink/canonical path, root transitions, 10k/100k corpora, peak RSS: UNVERIFIED. One Linux symlink-escape smoke produced no hit.

## Not implemented; **DO NOT** mark PASS

- Daily indexing: last-success timestamp, persistence, 24-hour scheduler, dedup/retry, and actual successful commit recognition remain NOT IMPLEMENTED. In this Fix5 runtime, `generation` remained 2 after successful reindex operations, so a generation-change heuristic cannot represent successful indexing. Worker exit code alone does not prove durable commit success. Private Fix5 source is needed to repair success reporting/persistence without false publication.
- Additional `/inside` system path and root-filter enforcement in Private Runtime: NOT IMPLEMENTED. The current Public `RuntimeClient` forwards candidate IDs to the ABI; Private must restrict candidate scans **before** any result cap. The uploaded archive has no Fix5 Private source.
- Full Unicode NFC (outside algorithmic Hangul composition): NOT IMPLEMENTED.
- GitHub commit, Actions, Windows artifacts: NOT PERFORMED.

## Compatibility and packaging

Public ABI v1 is unchanged. No TopoDB implementation or Private SDK is in the source-only ZIP. The unchanged Private Fix5 DLL/SO are provided separately in an explicitly labeled `RuntimeOnly_REFERENCE_UNMODIFIED` ZIP; they are not patched or newly compiled. A working runtime is needed for non-stub GUI/CLI smoke.
