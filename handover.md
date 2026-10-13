# TrueType UI rendering handover

## Goal

Render all game UI text with a configurable Windows TrueType font through the Bink proxy, while keeping the existing `game.fnt` renderer as the fail-open fallback. The current Bink-only live renderer is an experimental prototype and still needs visual testing in RA2 and Yuri's Revenge.

## Current project and evidence

- The game normally draws UI text from its custom `fonT` bitmap font (`game.fnt`). Thai glyphs use CP874 byte slots (`0xA1`–`0xFB`), and the layout code widens those bytes into 16-bit units before rendering.
- The supported game builds and hook signatures, calling conventions, font offsets, and 16-bit surface behavior are documented in [`research-notes.md`](research-notes.md). Runtime hooks continue to validate executable signatures before patching.
- The existing bitmap fix adjusts Thai glyph advances and mark placement, including tone marks above Sara Am. It uses private font data and remains available as fallback. See [`ddraw/README.md`](ddraw/README.md).
- The Bink proxy adds missing `[TrueType]` defaults to the adjacent INI on startup without replacing existing values. The standalone test build enables rendering by default; `[TrueType] Render=0` disables it. It collects CP874 glyphs, per-character colors, and game-computed positions during layout/direct-text calls. On the final glyph callback, while the game's surface is still active, it restores the saved background and overlays Uniscribe/GDI-rendered lines into the 16-bit surface.
- This replaces the visible text for supported runs, including non-Thai UI strings. The game still uses its FNT widths and wrapping for layout. The installer payload has not been changed.
- Configure the font in `thaifontfix.ini`:

  ```ini
  [TrueType]
  Render=1
  Font=Arial
  Style=Regular
  Size=12
  ```

- `Style` accepts `Regular`, `Bold`, `Italic`, and `BoldItalic` (or `Bold Italic`). `Size` accepts 8–64 pixels; `0` uses the game font height. It affects glyph size but does not change FNT-driven layout, line spacing, or dropdown item height. Preview capture remains separate with `Preview=1`.

## Test plan and limits

1. Copy the built Bink DLL into a test game installation and set the INI section above. Enable `DebugLog=1` and confirm the log reports the proxy, hooks, and live TrueType rendering.
2. Check English and Thai text across menus, dialogs, tooltips, multiplayer/lobby UI, and Yuri's Revenge. Include Thai clusters such as `ดำ`, `ต่ำ`, and above-vowel plus tone-mark combinations.
3. Compare text bounds and line wrapping against the bitmap font. Current layout and advances are still FNT-based, so a different face may look wider/narrower inside the old layout boxes.
4. Confirm unsupported/control-heavy runs remain readable via the original FNT fallback. Per-character colors are preserved by mapping Uniscribe character positions back onto the shaped line. The prototype fails open if surface/clip bounds, CP874 conversion, shaping, or rasterization cannot be validated.
5. Only after successful visual tests should this path be considered for installer packaging or enabled by default.

## Constraints and open questions

- The test DLL was built with the available VS2022 x86 v143 toolset because v141_xp is unavailable here. The PE subsystem is marked 5.01, but XP-compatible imports/runtime behavior has not been established; treat this DLL as a modern-Windows test build.
- The code uses Uniscribe and GDI rather than DirectWrite to keep the API approach compatible with older Windows versions, but this particular build is not certified for XP.
- The renderer preserves the game's packed 16-bit foreground color and clamps its capture to the font clip rectangle. The target surface lock lifetime and renderer-specific behavior still require in-game testing.
- Runs with unsupported controls, multiple per-glyph colors, or failed surface/shaping checks fall back to the original FNT output. Pixel-perfect alignment, glyph coverage, font fallback, and performance across all UI screens remain to be evaluated.
- Do not change installer payloads or the game directory while validating the prototype.
