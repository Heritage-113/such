# Such

**Such, a great search tool.**

Finding a file should not feel like archaeology.

Such is a native cross-platform file search app built around one idea:

> Search first. Everything else gets out of the way.

No giant file tree.  
No dashboard.  
No Electron.  
No fifty buttons explaining how powerful the search engine is.

Just type what you remember and get the file.

```text
report
/pdf report
/pin
/drive
```

On macOS, Linux, and iPadOS, command-style filters use `//` instead of `/`.

## What it does

- Native desktop UI
- Fast local file search
- Filename and path search
- Extension and date filters
- Persistent pins
- Native file icons
- Search-root switching with `/drive`
- Additional roots with `/index`
- Font selection with `/font`
- Windows, Linux, macOS, and iPadOS frontend targets
- Stable runtime ABI for production search

## Philosophy

Such is intentionally boring on screen.

The search field is the interface.  
Management stays behind commands and gestures.  
The engine can be complicated. The screen should not be.

```text
Fast because it does less.
```

## Repository layout

This public repository contains the Such frontend, CLI, installer logic, runtime client, and stable public ABI.

The production search runtime is distributed separately. Its implementation is intentionally not part of this repository.

The current SourceOnly package is included as a ZIP in the repository root.

## Building

See [`BUILDING.md`](BUILDING.md).

## About

Such is developed by **Heritage Inc.**

Practical tools. Native code. Fewer buttons.
