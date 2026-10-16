# Such Windows setup

Windows x64 user-mode installer (Inno Setup 7.1.0). The repository keeps the setup definition; generated `.exe` files stay in ignored `dist/installers/`.

Run `scripts/build_installer_windows.cmd` from Explorer or from PowerShell 7:

```
.\scripts\build_installer_windows.ps1 -Config Release -Arch x64
```

If Inno Setup 7 is not installed, the build script automatically downloads the official pinned 7.1.0 x64 installer, validates SHA-256 and Pyrsys B.V. Authenticode, and silently provisions the compiler for the current user under ignored `.cache/toolchains/InnoSetup7/`. No Inno Setup 6 compiler is accepted. To avoid downloading, use `-OfflineOnly` (the compiler must already be installed); to choose a compiler, pass `-ISCCPath "C:\...\ISCC.exe"`. No Docker, machine-wide admin elevation, service or autostart registration is used.

The entry point always executes the normal Windows verification + stage pipeline to prevent reusing an old binary after source changes; it checks the version in the generated GUI/CLI PE resources before packaging. It requires the hash-pinned `runtime/windows-x64/SuchRuntimePrivate.dll` from the SourceOnly ZIP. The private runtime is included beside both the main EXE and the CLI executable; its SHA-256 is checked immediately before packaging.

Source of truth for the installer version and EXE version resources is the top-level `VERSION`. The generated filename is `dist/installers/Such_v<version>_Windows_x64_Setup.exe`. Never commit the installer binary.

Per-user install: `%LOCALAPPDATA%\Programs\Such`. User index state is not included in the setup payload and is not removed on uninstall. Start Menu shortcut, optional desktop shortcut and optional per-user PATH registration are supported. A preexisting PATH entry is not claimed for removal.

**Verification boundary**: Linux cannot run `ISCC.exe` or qualify the Windows GUI installer. Run `scripts/verify_windows.cmd` and build/install/uninstall the generated setup on Windows x64 before calling the installer production-ready. The existing PowerShell user-install workflow is independent and remains supported. Avoid using both uninstallers interchangeably on the same directory without first checking the installed-app registration.

Toolchain provisioning requires an internet connection on first use and does not register a service. The Inno Setup download is not included in SourceOnly; product Runtime DLL/SO are included. Inno Setup requests a commercial license for commercial use; handle that separately from technical installation.
