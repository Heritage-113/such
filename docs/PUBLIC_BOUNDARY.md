# Public repository boundary

This repository contains frontend code and the stable runtime client ABI only.

Production search/storage implementation, private dependencies, benchmarks, validation data, debug symbols, and private build machinery stay in a physically separate private tree.

Production desktop releases inject one approved runtime binary at packaging time:

- Windows x64: `SuchRuntimePrivate.dll`
- Linux x64: `libSuchRuntimePrivate.so`

The public source build uses `SuchRuntimeStub` for contract tests. `SuchMCP` is optional and bootstrapped on demand. Font bundles are optional. macOS desktop is not supported.
