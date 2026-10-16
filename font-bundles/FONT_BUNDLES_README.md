# Local font bundles (not redistributed)

This SourceOnly archive intentionally contains **no third-party font binaries**.

For one-command Windows installation, place locally authorized font bundles here before running:

```powershell
scripts\install_windows.cmd -Config Release -Arch x64
```

Recognized inputs are `.zip`, `.ttf`, and `.otf`. The installer recursively extracts ZIP archives and installs font files for the current Windows user.

The development set used for Such includes bundles such as:

- `Gowun-Batang-master.zip`
- `KOPUBWORLD_OTF_FONTS2026.zip`
- `continuous.zip`
- a locally obtained Google Sans Flex TTF/OTF/ZIP, if desired

Alternatively keep the bundles elsewhere and run:

```powershell
scripts\install_windows.cmd -Config Release -Arch x64 -FontSourceDir "X:\Fonts\Such"
```

Use `-SkipFonts` to install only the application, or `-RequireFonts` to fail the installation when no local font source is found.
