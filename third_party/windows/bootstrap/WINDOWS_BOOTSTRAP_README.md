# Windows build bootstrap assets

Windows build dependencies are owned by `scripts/bootstrap_windows.ps1`. Generated toolchains and offline layouts stay outside the source tree so long release/extraction paths do not propagate into MSBuild/FileTracker paths.

PowerShell host setup happens before the build bootstrap: every `.cmd` entry goes through `scripts/windows_powershell_host.cmd`, which materializes the pinned PowerShell 7.6.6 LTS portable host after SHA-256 verification. The asset/version authority is `scripts/windows_powershell_assets.txt`. Windows PowerShell 5.1 is only the first-run extraction/download bridge.

The build bootstrap order is:

1. reuse an installed CMake >= 3.20 when it is usable;
2. otherwise use the pinned portable CMake 3.31.12 archive from the external offline root/cache;
3. on a connected host, download that pinned archive and verify SHA-256 before extraction;
4. reuse a **Visual Studio 2022 (17.x)** C++ toolchain when present;
5. otherwise install VS2022 Build Tools from the prepared offline layout or the signed Microsoft bootstrapper.

Default generated/cache locations:

```text
%USERPROFILE%\STC                 portable CMake / bootstrap cache
%USERPROFILE%\STC\offline        offline dependency root
%USERPROFILE%\STC\offline\VS17  Visual Studio 2022 offline layout
```

Overrides:

```text
SUCH_TOOLCHAIN_ROOT
SUCH_WINDOWS_OFFLINE_ROOT
SUCH_VS_OFFLINE_LAYOUT
```

For extremely path-sensitive machines, `C:\STC` or `C:\S\TC` is recommended when the current user can write there.

The multi-gigabyte Visual Studio layout must **not** be generated under the Such repository. Use `scripts\prepare_windows_offline_toolchain.cmd` on a connected Windows machine, then copy the prepared external offline root to the disconnected machine.

The source ZIP does not claim to contain the Visual Studio offline layout. `-OfflineOnly` requires a previously prepared external layout when VS2022 C++ Build Tools are not already installed.

PowerShell offline seeding:

- put the matching official `PowerShell-7.6.6-win-*.zip` in this directory, or
- put it in `%USERPROFILE%\STC\offline` / `SUCH_WINDOWS_OFFLINE_ROOT`.

The host shim verifies the pinned Microsoft/GitHub release SHA-256 before extraction and does not require an elevated MSI installation.
