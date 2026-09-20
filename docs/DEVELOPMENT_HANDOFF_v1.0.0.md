# Such v1.0.0 Public Handoff

## Repository role

GitHub-safe public frontend: UI, CLI, query syntax, installers, stable C runtime ABI, and `RuntimeClient`. The production search/storage implementation remains private.

## Supported targets

- Windows x64
- Linux x64
- iPadOS source path remains separate from desktop support
- **macOS desktop: unsupported and intentionally removed**

Do not reintroduce a macOS desktop build/runtime path by default.

## Production runtime boundary

- Windows: `SuchRuntimePrivate.dll`
- Linux: `libSuchRuntimePrivate.so`

`SUCH_RUNTIME_LIBRARY` overrides discovery for development/testing. ABI version: 1. Release builds verify pinned SHA-256 values before packaging.

`SuchMCP` is an optional on-demand integration fetched from its separate public repository. It is not a required Such release artifact. Font bundles are optional.

## Search root commands

- Windows `/index` adds a root; `/drive` replaces all roots.
- Linux `//index` adds a root; `//drive` replaces all roots.

## Publication rule

Publish this tree itself. Never create the public repository by copying the private runtime tree and relying on ignore rules.
