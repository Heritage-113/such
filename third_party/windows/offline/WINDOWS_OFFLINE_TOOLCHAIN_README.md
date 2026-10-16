# Windows offline toolchain pointer

The Visual Studio 2022 Build Tools network layout is intentionally stored **outside** the Such source tree. Its deeply nested package paths can exceed Windows/MSBuild/FileTracker limits when combined with a long checkout.

Default external locations:

```text
%USERPROFILE%\STC\offline
%USERPROFILE%\STC\offline\VS17
```

Prepare on a connected Windows machine:

```bat
scripts\prepare_windows_offline_toolchain.cmd
```

For a shorter path, set for example:

```bat
set SUCH_WINDOWS_OFFLINE_ROOT=C:\STC
```

or:

```bat
set SUCH_VS_OFFLINE_LAYOUT=C:\STC\VS17
```

Copy the prepared offline root to the disconnected machine at the same path (or set the corresponding variable there), then run:

```bat
scripts\bootstrap_windows.cmd -OfflineOnly
```

`-OfflineOnly` never falls back to a network download. The source archive itself does not contain the multi-gigabyte Visual Studio layout.
