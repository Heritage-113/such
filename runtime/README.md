# Runtime artifacts

The public source tree does not contain the production search/storage engine implementation.

A production desktop release needs exactly one approved runtime for its target platform:

```text
Windows x64  .runtime/windows-x64/SuchRuntimePrivate.dll
Linux x64    .runtime/SuchRuntimePrivate_v0.6.2_Linux_x64_RuntimeOnly/libSuchRuntimePrivate.so
```

Approved SHA-256 values are in `RUNTIME_SHA256_v1.0.0.txt`. Release scripts reject missing or mismatched binaries where strict runtime validation is enabled.

Linux runtime legal files (`LEGAL_NOTICE.txt` and `licenses/`) are distributed with the Linux runtime package and should remain with the corresponding release artifacts.

## License boundary

The public Such source code is licensed under Apache License 2.0 as described by the repository `LICENSE` and `NOTICE` files.

The production runtime binaries under `.runtime/` are separate Heritage Inc. artifacts and are **not licensed under Apache-2.0** merely because they are stored in this repository. Their use or redistribution is subject to separate terms from Heritage Inc.

The private Such SDK is also outside the public Apache-2.0 grant and is provided separately on request/arrangement.

No other private binary is required for the desktop frontend. `SuchMCP` is optional and bootstrapped on demand. Font bundles are optional. macOS desktop is unsupported.

For SDK access, commercial embedding, or private runtime integration, contact Heritage Inc. through https://heritage-labs.net.
