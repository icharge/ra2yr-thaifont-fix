# BINKW32 proxy

This integration uses the game's `BINKW32.DLL` import as an app-local loader point. It does not replace DirectDraw and can coexist with CnCNet's renderer selection, the DDraw integration, and Syringe. The Bink proxy starts the shared Thai font hooks on a worker thread after DLL load, and forwards every export from the installed original Bink DLL.

The installer renames the original game DLL to lowercase `binkw32.bin` and installs lowercase `binkw32.dll` as the proxy. The proxy loads the `.bin` file by its full path. Only those two Bink files are left in the game folder; installation does not create proxy-copy or SHA-256 sidecar files. Restoring puts the original DLL back under `binkw32.dll`.

## Build and install

From the repository root, with the RA2 game directory one level above it:

```powershell
.\bink\build.ps1
.\bink\install-bink-proxy.ps1
```

The build reads the original `binkw32.dll` export table (or `binkw32.bin`, `BINKW32.TLB`, or the legacy `ThaiFontFixBinkReal.dll` if already installed), produces an x86 proxy, and verifies that every original export name and ordinal is forwarded. It requires Visual Studio 2022 Build Tools with the x86 C++ tools and Windows SDK `10.0.26100.0`.

Close RA2/Yuri's Revenge, CnCNet, and Syringe before installing or restoring. The original game DLL is preserved beside the proxy. To restore it:

```powershell
.\bink\install-bink-proxy.ps1 -Restore
```

Use `-GameDirectory <path>` when the game is installed somewhere else. The proxy uses the shared `thaifontfix.ini` and appends diagnostics to `thaifontfix.log` beside the game DLLs.

## Loader behavior and limits

The supported `game.exe`, `GameMD.exe`, and `gamemdpatch.exe` import `BINKW32.DLL`. The CnCNet launcher only hands off to those engines; the Bink proxy is loaded by the engine process. This is a candidate path verified from the supplied game's PE import/export tables, but its actual load and hook behavior must still be confirmed in a game run by checking `thaifontfix.log` for the proxy module path and hook messages.

The proxy preserves each original export name and ordinal. Small x86 stubs resolve the matching export from `binkw32.bin` by its full game-folder path and tail-jump to it; this avoids relying on Windows to locate a renamed DLL from a PE forwarder string. The proxy does not emulate or modify Bink video/audio behavior. Like the other external-loader variants, the shared font-hook code still validates the game executable and live font state before changing rendering. Existing installs using the earlier `.TLB` or `ThaiFontFixBinkReal.dll` backup name are restored and migrated by the installer.
