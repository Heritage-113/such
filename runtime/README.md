# Runtime artifacts

The public source tree does not contain the production search/storage engine implementation.

Desktop builds load one approved runtime for their target platform:

```text
Windows x64  .runtime/windows-x64/SuchRuntimePrivate.dll
Linux x64    .runtime/linux-x64/libSuchRuntimePrivate.so
```

Approved SHA-256 values are in `RUNTIME_SHA256_v1.1.7.txt`. Release scripts reject missing or mismatched binaries where strict runtime validation is enabled.

The public runtime boundary remains ABI v1. Content search is an optional ABI-v1 extension: ordinary file search continues to work with a runtime that does not export the content-search symbol, while `/inside` requires a compatible runtime that does.

Linux runtime legal files (`LEGAL_NOTICE.txt` and `licenses/`) remain with the corresponding release artifacts.

## License boundary

The public Such source code is licensed under Apache License 2.0 as described by the repository `LICENSE` and `NOTICE` files.

The production runtime binaries under `.runtime/` are separate Heritage Inc. artifacts and are **not licensed under Apache-2.0** merely because they are stored in this repository. Their use or redistribution is subject to separate terms from Heritage Inc.

The private Such SDK is also outside the public Apache-2.0 grant and is provided separately on request/arrangement.

No other private binary is required for the desktop frontend. `SuchMCP` is optional and bootstrapped on demand. macOS desktop is unsupported.

For SDK access, commercial embedding, or private runtime integration, contact Heritage Inc. through https://heritage-labs.net.
