# Runtime artifacts

The public source tree does not contain the production search engine.

A production desktop release needs exactly one private runtime for its target platform:

```text
Windows x64  .runtime/windows-x64/SuchRuntimePrivate.dll
Linux x64    .runtime/linux-x64/libSuchRuntimePrivate.so
```

Approved SHA-256 values are in `RUNTIME_SHA256_v1.0.0.txt`. Release scripts reject missing or mismatched binaries.

Linux runtime legal files (`LEGAL_NOTICE.txt` and `licenses/`) should be placed beside the injected `.so`; the Linux build copies them into the release documentation directory.

No other private binary is required. `SuchMCP` is optional and bootstrapped on demand. Font bundles are optional. macOS desktop is unsupported.
