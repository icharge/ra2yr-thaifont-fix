# BINKW32 proxy

This integration uses the game's `BINKW32.DLL` import as an app-local loader point. It does not replace DirectDraw and can coexist with CnCNet's renderer selection, the DDraw integration, and Syringe. The Bink proxy starts the shared Thai font hooks on a worker thread after DLL load, and forwards every export from the installed original Bink DLL.

The installer renames the original game DLL to lowercase `binkw32.bin` and installs lowercase `binkw32.dll` as the proxy. The proxy loads the `.bin` file by its full path. Only those two Bink files are left in the game folder; installation does not create proxy-copy or SHA-256 sidecar files. Restoring puts the original DLL back under `binkw32.dll`.

## Build and install

### Visual Studio

Open [`bink.vcxproj`](bink.vcxproj) in Visual Studio 2022 and build **Release | Win32**. The project compiles the Bink proxy and shared ThaiFontFix C++ source with MSVC, generates the proxy export table from the game's original Bink DLL before compilation, then verifies and copies the result to `binkw32-proxy.dll`.

The default game directory is two levels above `bink.vcxproj`, matching this repository's layout. For another install, set the project property `ThaiFontFixGameDirectory` to the game folder. The default Visual Studio tools path is `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`; set `ThaiFontFixVisualStudioPath` if Build Tools is installed elsewhere.

### PowerShell build

From the repository root, with the RA2 game directory one level above it:

```powershell
.\bink\build.ps1
.\bink\install-bink-proxy.ps1
```

The script uses the same MSVC compiler and validation steps. Both build methods read the original `binkw32.dll` export table (or `binkw32.bin`, `BINKW32.TLB`, or the legacy `ThaiFontFixBinkReal.dll` if already installed), produce an x86 proxy, and verify that every original export name and ordinal is forwarded. They require the Visual C++ v143 x86 tools and Windows SDK `10.0.26100.0`.

Close RA2/Yuri's Revenge, CnCNet, and Syringe before installing or restoring. The original game DLL is preserved beside the proxy. To restore it:

```powershell
.\bink\install-bink-proxy.ps1 -Restore
```

Use `-GameDirectory <path>` when the game is installed somewhere else. The proxy uses the shared `thaifontfix.ini` and appends diagnostics to `thaifontfix.log` beside the game DLLs.

## Loader behavior and limits

The supported `game.exe`, `GameMD.exe`, and `gamemdpatch.exe` import `BINKW32.DLL`. The CnCNet launcher only hands off to those engines; the Bink proxy is loaded by the engine process. This is a candidate path verified from the supplied game's PE import/export tables, but its actual load and hook behavior must still be confirmed in a game run by checking `thaifontfix.log` for the proxy module path and hook messages.

The proxy preserves each original export name and ordinal. Small x86 stubs resolve the matching export from `binkw32.bin` by its full game-folder path and tail-jump to it; this avoids relying on Windows to locate a renamed DLL from a PE forwarder string. The proxy does not emulate or modify Bink video/audio behavior. Like the other external-loader variants, the shared font-hook code still validates the game executable and live font state before changing rendering. Existing installs using the earlier `.TLB` or `ThaiFontFixBinkReal.dll` backup name are restored and migrated by the installer.
