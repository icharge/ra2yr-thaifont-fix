# Syringe integration

This integration packages the shared ThaiFontFix hooks as Ares Syringe extensions. It supports the supplied Red Alert 2 1.006 `game.exe` and Yuri's Revenge `gamemd.exe`. The handshake checks file size, PE timestamp, and CRC before allowing an extension to install its entry-point hook.

Both variants use a `.dll.inj` sidecar and a five-byte entry-point hook. The Syringe entry hook runs the same runtime font fix as the DirectDraw and Bink builds. A shared `ThaiFontFixIsActive` export lets integrations skip a duplicate installation only if another loaded module actually reports active hooks; the loader checks the marker export in addition to the DLL name.

## Build

From the repository root:

```powershell
.\syringe\build.ps1
```

The build requires Visual Studio 2022 Build Tools with the x86 C++ tools and Windows SDK. It produces:

- `syringe/build-artifacts/ThaiFontFixSyringe-RA2/ThaiFontFixSyringe-RA2.dll`
- `syringe/build-artifacts/ThaiFontFixSyringe-YR/ThaiFontFixSyringe-YR.dll`

The hook sidecars and config templates stay in this folder. No YR++ headers are needed because the extension uses Syringe's handshake and `.dll.inj` interface directly.

## Install and restore

Close the game and Syringe before installing. From the repository root:

```powershell
.\syringe\install-syringe.ps1
```

By default, the installer copies both variants, allowing the same game directory to launch either RA2 or Yuri's Revenge through Syringe. To install one variant only, pass `-Variant RA2` or `-Variant YR`. Use `-GameDirectory <path>` for a different game folder. The installer refuses to overwrite existing files and preserves any existing per-DLL INI settings.

To remove the installed DLLs and hook sidecars:

```powershell
.\syringe\install-syringe.ps1 -Restore
```

Restore verifies file hashes and leaves modified DLLs or sidecars untouched. It keeps INI files and logs so settings and diagnostics remain available.

## Settings and diagnostics

Both variants read the shared `thaifontfix.ini` and append diagnostics to `thaifontfix.log` beside the game executables.

The shared template enables diagnostics by default. `Enabled` turns the fix on or off. `CenterMarks` controls horizontal mark positioning, `ToneMarkRaise` controls the vertical offset for stacked marks including Sara Am, and `MaxExtraSpacing` sets the maximum font spacing handled by the fix. Set `DebugLog=1` to write the shared log and OutputDebugString messages.

Successful hook startup logs `font hooks installed`. When Thai text appears, look for layout/render hook messages, an accepted font state, and `Thai mark handled`. The detailed font format and CP874/Sara Am behavior are documented in [`../research-notes.md`](../research-notes.md) and [`../ddraw/README.md`](../ddraw/README.md).

## Compatibility with other integrations

The Syringe extension and local DirectDraw/Bink loaders can coexist in the same game folder. If one integration has already installed the font hooks, another skips them after calling its `ThaiFontFixIsActive` export and receiving an active result. A DLL named `ddraw.dll` or `binkw32.dll` without that export does not count as active. Do not run multiple old proxy builds that lack the marker, since they cannot participate in duplicate detection.
