# Fast filename indexing and TopoDB shards (Windows)

The worker publishes an immutable filename checkpoint after directory enumeration and TopoDB metadata reconciliation. The GUI reads that checkpoint without opening the writer database, so filename/path queries can work while document-content indexing continues. `/inside` reports that contents are still indexing until the worker finishes. The complete catalog/content generation retains its atomic commit and recovery boundary. If content processing fails, the filename checkpoint remains readable and the GUI reports the failure.

## Commands

- `/index`: rebuild the current folders with the saved setting.
- `/index auto`: restore automatic selection.
- `/index 1`, `/index 2`, `/index 4`, `/index 8`, `/index 16`: rebuild using that many independent TopoDB shards. The setting survives restart.
- `/index <folder>`: retain the existing add-folder behavior. Quote a purely numeric folder name to distinguish it from a shard choice.

Automatic selection uses one DB below 131072 rows. Larger indexes use four DBs with at least eight hardware threads, two DBs with at least four hardware threads, and one otherwise. Eight and sixteen remain explicit choices because more DBs can increase build overhead. `SUCH_INDEX_SHARDS=auto|1|2|4|8|16` is also supported for advanced launches.

Rows are assigned by stable file ID hash; each row belongs to exactly one shard. Searches merge shard-local indexes back into the canonical row sequence. Independent DB writes can run in parallel with a shared worker budget. A state-wide writer lease prevents competing writers even across shard-count changes. Snapshot readers never acquire that lease or open the writer DB.

## Validation on 2026-10-16

Runtime CTest: 6/6, including shard result membership and reopen checks for 2/4/8/16 DBs, plus filename checkpoint access after content failure and read-only mutation rejection.

Separate-process integration: 2400 text files, filename query available at 0.468 s, complete content indexing at 13.626 s. Verified simultaneous reader access, duplicate-writer rejection, saved 8-shard choice, and a cold reopen. These timings describe this fixture, not a whole-drive speed guarantee.

Synthetic 524288-row metadata benchmark, two runs on this machine:

| DBs | Initial build (ms) | 1% metadata update (ms) | Selective query (ms) |
| --- | ---: | ---: | ---: |
| 1 | 545–552 | 425–456 | 3.83–3.92 |
| 2 | 407–409 | 290–291 | 2.45–2.53 |
| 4 | 347–350 | 171–181 | 1.93–1.96 |
| 8 | 379–392 | 147–155 | 1.67 |
| 16 | 1100–1263 | 289–330 | 2.25–2.49 |

This benchmark measures the TopoDB/metadata stage. Disk traversal and document parsing are excluded. No claim of equivalent end-to-end indexing speedup is implied. Directory traversal remains worker-owned, excluded folders are pruned before recursion, and locked content streams are skipped without discarding filename metadata.