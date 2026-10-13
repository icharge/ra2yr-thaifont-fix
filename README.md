# Thai bitmap-font fix for RA2 / Yuri's Revenge

ThaiFontFix adjusts placement of Thai CP874 glyphs in the game's bitmap-font renderer. It supports the supplied Red Alert 2 and Yuri's Revenge executable builds and does not modify the game executable. The Bink installer offers the current font and six additional `game.fnt` choices; the Thai glyph bitmaps from the current font are merged into the additional choices when the setup is built. Setup restores the player's original font when uninstalled.

## Integrations

- [bink/](bink/README.md) is the tested app-local loader. Its installer installs lowercase binkw32.dll and the packaged `game.fnt`, preserves the original DLL as binkw32.bin, and backs up the original font for restore. It works for direct or CnCNet launches without replacing the renderer.
- [ddraw/](ddraw/README.md) integrates the fix into CnCNet's TS-DDraw renderer.
- [syringe/](syringe/README.md) provides separate Syringe extensions for RA2 and Yuri's Revenge.

The experimental version.dll integration was removed because it did not load reliably in the game. Its import table alone was not enough to make it a dependable loader.

The Bink, DDraw, and Syringe variants share the implementation in [shared/ThaiFontFix.cpp](shared/ThaiFontFix.cpp), use [thaifontfix.ini](thaifontfix.ini), and write diagnostics to thaifontfix.log beside the game executables. Their active-hook marker prevents duplicate installation when variants are present together. The installer obtains its additional font choices and their credits from the [FontsForRedAlert2 submodule](fonts/FontsForRedAlert2); Thai glyphs are merged into package copies, so the upstream font files remain unchanged. Initialize the submodule before building the setup.

## Research and preview

[research-notes.md](research-notes.md) records the Ghidra findings, font layout, CP874 behavior, and Sara Am placement logic. [render_fnt_test.py](render_fnt_test.py) renders the actual game.fnt bitmap records for a placement preview; see [thai-render-sara-am.png](thai-render-sara-am.png). The preview is useful for glyph-layout checks, but it does not execute the game renderer or DLL hooks.

## Build

Each supported integration has its own build and install instructions. The C++ builds require Visual Studio 2022 Build Tools with the x86 toolchain and Windows SDK. DDraw also requires initializing the pinned TS-DDraw submodule.
