# BINKW32 proxy

This integration uses the game's `BINKW32.DLL` import as an app-local loader point. It does not replace DirectDraw and can coexist with CnCNet's renderer selection, the DDraw integration, and Syringe. The Bink proxy starts the shared Thai font hooks on a worker thread after DLL load, and forwards every export from the installed original Bink DLL.

The installer renames the original game DLL to lowercase `binkw32.bin` and installs lowercase `binkw32.dll` as the proxy. It also installs the packaged default `game.fnt`; an existing font is preserved as `game.fnt.thaifnt-original` and restored during uninstall. The installer package stores the proxy under that same game loader filename. The proxy loads the `.bin` file by its full path. Installation does not create proxy-copy or SHA-256 sidecar files. Restoring puts the original DLL back under `binkw32.dll` and restores the original font.

## Build and install

### One-file NSIS installer

Install NSIS 3.x (the `makensis.exe` command), then build the standalone installer from the repository root:

```powershell
.\bink\installer\build-installer.ps1
```

The script rebuilds the proxy for the selected game installation and packages the proxy, default INI, and default font into `bink/installer/ThaiFontFix-Setup.exe`. Pass `-NsisPath <path-to-makensis.exe>` if NSIS is not on `PATH`. The NSIS installer supports Windows XP and later; the proxy itself targets Windows XP SP3 and later when built with the `v141_xp` toolset.

Users run the setup, accept the detected game folder or use **Browse**, then review the detected installation state. After the folder is confirmed, setup identifies ThaiFontFix by its exported marker in `binkw32.dll`, displays the installed proxy version alongside the setup version, and checks for exactly one preserved original (`binkw32.bin`, with support for older backup names). If installed, **Uninstall** is selected by default; otherwise **Install** is selected. Choosing **Install** for an existing proxy asks for confirmation, replaces only `binkw32.dll`, and keeps the original Bink backup and current font. Setup can recover an update interrupted after the old proxy was staged. If other proxy and backup files are missing or inconsistent, setup explains the state and blocks changes until the folder is corrected. Auto-detection checks the Westwood game registry entries first (both 32-bit and 64-bit views on 64-bit Windows, under HKLM and HKCU), then Steam library registry paths, the setup folder and its parents, and standard Westwood folders. Registry `InstallPath` values may name either the game folder or an executable within it. Every accepted folder must contain `RA2.exe`, `game.exe`, `ra2.mix`, and `language.mix` together. Setup starts without administrator access and checks folder write access before changing game files; if Windows denies access, it relaunches setup with the same folder and action and requests administrator access. A new installation renames the existing `binkw32.dll` to lowercase `binkw32.bin`, installs the proxy as `binkw32.dll`, and creates `thaifontfix.ini` only when one is not already present. Before changing files, setup checks for running RA2/Yuri/Syringe processes, validates that both DLLs are x86, and compares each original named export and ordinal with the packaged proxy. It uses temporary staging names and attempts to roll back if a file operation fails. The setup creates no uninstaller; use **Uninstall** to restore the original files.

To package an already-built proxy without recompiling it, pass `-SkipProxyBuild`. This uses the existing `bink/build-artifacts/binkw32.def` generated for that proxy; use it only when the definition matches the DLL and the DLL was built with the XP target toolset.

To undo the installation, open the same installer, select the game folder, and click **Restore original**. It puts the preserved Bink DLL back as `binkw32.dll`, restores the original `game.fnt`, and keeps the INI settings. Close RA2/Yuri's Revenge and Syringe first. Setup requests administrator access only when the selected folder requires it.

### Visual Studio

Open [`bink.vcxproj`](bink.vcxproj) in Visual Studio and build **Release | Win32**. It uses the `v141_xp` toolset so the proxy can load on Windows XP SP3 and later. In Visual Studio Installer, add **C++ Windows XP Support for VS 2017 (v141) tools** if that component is missing. The project compiles the Bink proxy and shared ThaiFontFix C++ source with MSVC, generates the proxy export table from the game's original Bink DLL before compilation, then verifies and copies the result to `binkw32.dll`.

The default game directory is two levels above `bink.vcxproj`, matching this repository's layout. For another install, set the project property `ThaiFontFixGameDirectory` to the game folder. The default Visual Studio tools path is `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`; set `ThaiFontFixVisualStudioPath` if Build Tools is installed elsewhere.

### PowerShell build

From the repository root, with the RA2 game directory one level above it:

```powershell
.\bink\build.ps1
.\bink\install-bink-proxy.ps1
```

The script uses the same MSVC compiler and validation steps. Both build methods read the original `binkw32.dll` export table (or `binkw32.bin`, `BINKW32.TLB`, or the legacy `ThaiFontFixBinkReal.dll` if already installed), produce an x86 proxy, and verify that every original export name and ordinal is forwarded. They require the Visual Studio `v141_xp` x86 tools and Windows SDK `10.0.26100.0`. The installer and proxy statically link the runtime, so they do not require a separate Visual C++ redistributable.

Windows XP support requires XP Service Pack 3 on 32-bit Windows. The project uses the Windows XP platform toolset and PE subsystem version `5.01`; build scripts also reject the known Vista-and-later CRT imports.

Close RA2/Yuri's Revenge, CnCNet, and Syringe before installing or restoring. The original game DLL is preserved beside the proxy. To restore it:

```powershell
.\bink\install-bink-proxy.ps1 -Restore
```

Use `-GameDirectory <path>` when the game is installed somewhere else. The proxy uses the shared `thaifontfix.ini`; when `DebugLog=1`, it clears `thaifontfix.log` during startup and appends diagnostics for that run beside the game DLLs.

## Loader behavior and limits

The supported `game.exe` and `gamemd.exe` import `BINKW32.DLL`. The CnCNet launcher only hands off to those engines; the Bink proxy is loaded by the engine process. This is a candidate path verified from the supplied game's PE import/export tables, but its actual load and hook behavior must still be confirmed in a game run by checking `thaifontfix.log` for the proxy module path and hook messages.

The proxy preserves each original export name and ordinal. Small x86 stubs resolve the matching export from `binkw32.bin` by its full game-folder path and tail-jump to it; this avoids relying on Windows to locate a renamed DLL from a PE forwarder string. The proxy does not emulate or modify Bink video/audio behavior. Like the other external-loader variants, the shared font-hook code still validates the game executable and live font state before changing rendering. Existing installs using the earlier `.TLB` or `ThaiFontFixBinkReal.dll` backup name are restored and migrated by the installer.

## TrueType UI rendering prototype

On startup, the Bink proxy adds any missing keys under `[TrueType]` to `thaifontfix.ini` without replacing existing values. The standalone test build enables live rendering by default; regular release and installer builds default it off. Defaults are Arial Regular with preview off. You can set a face/style explicitly or override `Render`:

```ini
[TrueType]
Render=1
Font=Arial
Style=Regular
Size=12
```

`Style` accepts `Regular`, `Bold`, `Italic`, or `BoldItalic` (also `Bold Italic`). `Size` is the rendered pixel height from 8 to 64; `0` uses the game's font height. The default face and style are Arial Regular, and the default size is `0`. Size changes the glyphs only; the game's layout, dropdown row height, and wrapping remain unchanged. For compact controls, try `Size=12`. `Preview=1` remains available separately to save the first eligible Thai sample as `thaifontfix-truetype-preview.bmp`.

When rendering is enabled, the Bink proxy captures the destination region before the first glyph, records the game's positions and per-character colors, and replaces the run on its final glyph callback while the surface is still active. The original FNT path supplies measurement and line breaks. The renderer falls back to the game's bitmap output when surface or clip bounds cannot be validated, a string contains unsupported control characters, or shaping/rasterization fails. Test this prototype in game; it is not included in the installer payload and has not been visually verified across all UI screens.
