# Public repository boundary

This repository contains the public Such frontend, CLI, build tooling, tests, documentation, and stable runtime client ABI.

The public source portions of this repository are licensed under the Apache License, Version 2.0, except where a file or directory is explicitly identified as being under separate terms.

## Private components

The following remain separately maintained by Heritage Inc. and are not part of the public Apache-2.0 source grant:

- production search/storage runtime implementation
- private backend source and private dependencies
- Such SDK implementation and SDK distribution packages
- internal benchmark infrastructure and private validation data
- private symbols, signing material, credentials, and private build machinery

The production runtime implementation is not present as source in this repository.

## Runtime artifacts

For release convenience, the repository may carry approved prebuilt production runtime binaries under `.runtime/`:

- Windows x64: `SuchRuntimePrivate.dll`
- Linux x64: `libSuchRuntimePrivate.so`

Those binaries are separate Heritage Inc. artifacts and are **not licensed under Apache-2.0** merely because they are stored in this repository. Their use or redistribution is subject to separate terms provided by Heritage Inc.

The public source build uses `SuchRuntimeStub` for contract tests and the stable runtime ABI for frontend/runtime separation.

## SDK

The Such SDK is private and is not distributed from this repository. Developers or organizations that need SDK access, native embedding support, private runtime integration, or commercial deployment terms should contact Heritage Inc. via https://heritage-labs.net.

## Other boundaries

`SuchMCP` is optional and bootstrapped on demand. Font bundles are optional. Desktop macOS support is intentionally excluded. iPadOS is a separate platform path.

Apache-2.0 does not grant trademark rights in Such, Heritage, Heritage Inc., or associated branding beyond the limited descriptive use provided by the license.
