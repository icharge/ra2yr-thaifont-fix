# TrueType Thai rendering handover

## Goal

Investigate whether ThaiFontFix can render Thai UI text with a Windows TrueType font, so Thai vowels and tone marks are shaped and positioned correctly. Keep the current bitmap-font fix as the compatibility fallback. This note is a research and implementation handover; no runtime TrueType renderer has been implemented yet.

## Current project and evidence

- The game uses a custom `fonT` bitmap font (`game.fnt`), not a Windows text API for its normal UI glyph rendering.
- The supplied font has 49-byte glyph records, 16 bitmap rows, and 29,674 records. Its Thai characters are reached through CP874 byte slots (`0xA1`–`0xFB`); direct Unicode Thai slots are absent.
- The game widens the legacy byte values into 16-bit character slots. The layout routine sees the full text and measures each glyph width plus font spacing; the glyph renderer receives one character at a time and draws a fixed-stride bitmap record.
- The relevant verified hook addresses, calling conventions, font data offsets, and executable checks are recorded in [`research-notes.md`](research-notes.md). Avoid relying on addresses without the existing executable signature checks.
- The current fix adjusts bitmap glyph advances and placement, including Thai combining marks and tone marks above Sara Am. It makes a private copy of font data when needed. See [`ddraw/README.md`](ddraw/README.md) for the behavior and limitations.
- Earlier feasibility research suggested an offline CP874-to-Unicode shaping and rasterization prototype is the safest first step. The in-game surface's palette/color, clipping, and drawing contract still need to be established before replacing glyph rendering.

## Main technical question

Can we shape complete Thai strings with Uniscribe, rasterize the resulting glyph runs with GDI, and transfer those pixels to the game's UI surface while matching its current clipping, palette/color, and spacing behavior?

The glyph hook alone is too late to shape a Thai cluster because it sees only one byte at a time. A runtime path would need to use the full string at the layout/direct-text hook, retain shaped cluster positions, then integrate those positions with the game's glyph drawing or draw a replacement run itself. Either approach must preserve the current x-advance and clipping contracts.

## Recommended investigation sequence

1. **Build an offline reference renderer.** Decode CP874 bytes to Unicode, shape Thai text with Uniscribe (`ScriptStringAnalyse` or the `ScriptItemize`/`ScriptShape`/`ScriptPlace` flow), and rasterize to a top-down DIB section. Compare the resulting pixels and measured advances with the existing FNT preview harness. Include at least `ดำ`, `ต่ำ`, and samples with above vowels plus tone marks.
2. **Record shaping and raster output.** Save the input bytes, Unicode sequence, shaped glyph IDs/advances/offsets, and rendered bitmap. Confirm mark ordering and baseline placement at the game's small UI font sizes.
3. **Inspect the game drawing contract.** Trace the glyph renderer and its callers to establish destination surface format/palette, foreground color, transparent-pixel rule, clipping rectangle, scaling, and whether the passed x coordinate is a pen position or glyph origin. Do this for the supported `game.exe` and `GameMD.exe` signatures.
4. **Choose an integration seam.** Prefer a per-string layout hook that shapes once and stores cluster placements for the matching render calls. Consider whole-run drawing only if the destination surface can be accessed safely and the game's color/clipping behavior can be reproduced. Preserve original rendering when conversion, shaping, or surface validation fails.
5. **Prototype in a disposable branch/worktree.** Keep the existing bitmap fix enabled as fallback and behind a configuration switch for experiments. Do not change installer payloads until a runtime prototype is verified in both RA2 and Yuri's Revenge.

## Constraints and open questions

- Windows XP is part of the stated compatibility target. Prefer Uniscribe plus GDI APIs available on XP; DirectWrite is not suitable for that baseline. Verify the exact APIs and behavior on the supported OS versions.
- The game's CP874 text is legacy encoded. Decide how unsupported bytes, missing glyphs, and font fallback should behave; do not pass raw CP874 bytes to a Unicode shaping API.
- Uniscribe can produce shaped glyph IDs and positions, but that alone does not solve transfer to the game's paletted or otherwise specialized UI surface.
- Determine whether a runtime TrueType path is justified versus an offline tool that rasterizes a chosen TrueType font into compatible FNT records. The latter could reuse the current renderer and hook architecture, but must preserve the game's expected character map and bitmap record format.
- Font selection is a future requirement. Any prototype should keep font choice separate from hook mechanics and avoid hard-coding one installed font name.

## Prototype success criteria

- CP874 samples decode to the intended Unicode text.
- Shaping yields correctly stacked Thai marks and stable advances for the game's UI size.
- The generated bitmap has a documented foreground/transparent-pixel representation and can be compared against the actual FNT output.
- A proposed runtime integration identifies how it preserves clipping, palette/color, x advances, and fallback behavior without changing the game files.
- The existing bitmap path remains available if a font, byte sequence, or surface cannot be handled.
