# Linux performance notes — Such v1.1

## Scope

These measurements were run on a controlled synthetic corpus to separate search latency from runtime startup cost.

Environment:

- Intel Xeon Platinum 8573C
- 5 logical CPUs visible
- 5.8 GiB RAM
- Linux 6.18.44 x86_64, glibc 2.41
- GNU findutils 4.10.0
- overlayfs for the full run
- tmpfs cross-check for the 100k-file load test
- benchmark runtime SHA-256 (historical benchmark provenance) `76f3080395e9352a3fc1840ff221e3325d0c5cf93cfc30c0809cd2bbfa3a0a6a`

CPU-profile rows were affinity-limited runs on the same host, not separate machines. GNU `find` timings used warm page cache. `cold-ish` runtime tests used `posix_fadvise(..., DONTNEED)` where available and are not equivalent to a reboot.

## Full-run summary

| Files | CPU | Index ms | Warm runtime reload ms | Cold-ish reload ms | Raw warm state read ms | State MiB |
|---:|:---|---:|---:|---:|---:|---:|
| 100,000 | 1 CPU | 827.01 | 101.28 | 102.25 | 1.83 | 21.73 |
| 100,000 | 2 CPU | 856.25 | 99.98 | 100.45 | 1.79 | 21.73 |
| 100,000 | 5 CPU | 794.43 | 103.27 | 105.81 | 2.49 | 21.73 |
| 500,000 | 1 CPU | 5293.46 | 560.68 | 556.24 | 13.02 | 108.62 |
| 500,000 | 2 CPU | 5090.27 | 625.34 | 673.95 | 12.42 | 108.62 |
| 500,000 | 5 CPU | 7650.40 | 904.50 | 950.07 | 15.74 | 108.62 |

The raw-read/reload gap is too large to explain as sequential filesystem I/O alone.

## tmpfs cross-check

At 100k files on tmpfs:

| CPU | Index ms | Warm reload ms | Cold-ish reload ms | Raw read ms |
|:---|---:|---:|---:|---:|
| 1 CPU | 752.69 | 105.03 | 98.35 | 2.37 |
| 5 CPU | 675.09 | 100.14 | 96.31 | 2.00 |

Moving the benchmark state to RAM did not materially reduce runtime reload time. This makes storage latency a poor explanation for the ~100 ms reload at 100k files.

## Search latency and pruning

At 500k files, 2-CPU affinity:

| Case | Results | Such median ms | GNU find median ms | Candidate chunks / total | Candidate rows |
|:---|---:|---:|---:|:---|---:|
| exact hit | 1 | 0.2811 | 304.29 | 1 / 7,813 | 64 |
| miss | 0 | 0.2436 | 341.98 | 0 / 7,813 | 0 |
| broad text | 21,740 | 21.9083 | 417.43 | 594 / 7,813 | 38,016 |
| extension | 125,000 | 100.5421 | 268.08 | 2,012 / 7,813 | 125,000 |
| recent date | 125,000 | 124.2823 | 752.38 | 1,968 / 7,813 | 125,000 |
| combined | 1,359 | 1.9389 | 347.39 | 39 / 7,813 | 2,496 |

All cases passed full-set correctness checks using sorted 64-bit BLAKE2b hashes of relative result paths.

## What the current data supports

The current measurements support these statements:

1. Persistent-runtime search itself is fast and the elimination layer substantially reduces candidate work for selective queries.
2. Broad filters naturally reduce the speedup because many rows must be materialized.
3. Linux runtime reload is not dominated by reading the state bytes from storage.
4. The poor 500k 5-CPU reload result suggests more CPUs do not automatically improve initialization and may introduce contention or memory-pressure effects.

The measurements do **not** prove which internal private-runtime function is responsible. Likely candidates include deserialization, container reconstruction, allocation, hashing, cache-unfriendly pointer chasing, or synchronization. A native profiler (`perf record`, `perf stat`, heap/allocation tracing) should be used before changing the storage format or adding a resident service.

## Next profiling sequence

Recommended order:

```text
1. perf stat around such_runtime_load_v1
2. perf record + flame graph for load only
3. count allocations/bytes during load
4. compare 1-thread vs normal loader
5. mmap/lazy-load prototype
6. resident IPC only after cold-start root cause is understood
```

A resident service may still be useful for CLI latency, but it should not be used as a substitute for understanding the reload cost.
