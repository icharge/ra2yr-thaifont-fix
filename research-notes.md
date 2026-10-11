# Reverse-engineering notes

- Ghidra 12.1.4 was run from `C:\app\reverse-tools` on the supplied x86 executables.
- Text layout: `game.exe` VA `0x00432490`; `GameMD.exe` / `gamemdpatch.exe` VA `0x00434CD0`.
- Glyph renderer: `game.exe` VA `0x004318E0`; `GameMD.exe` / `gamemdpatch.exe` VA `0x00434120`.
- Layout measures `glyph width + font spacing` and passes characters to the renderer. The renderer reads the width byte from a fixed-stride bitmap record and returns the next x coordinate.
- The layout routine uses `stdcall` and returns with `ret 0x28`; the renderer uses `thiscall` and returns with `ret 0x10`. The DLL shims preserve both stack-cleanup conventions.
- The font object points to parsed FNT data at `+0x04`; its spacing is at `+0x2c`. Parsed FNT data stores height at `+0x08`, record stride at `+0x14`, a 65,536-entry 16-bit glyph map at `+0x18`, glyph records at `+0x1c`, and unique glyph count at `+0x20`. The Thai fix validates and scans the CP874 byte slots `0x00..0xFF`, because the renderer receives those byte values widened to 16 bits.
- Supplied `game.fnt` has a 28-byte `fonT` header, 49-byte glyph records, 16 rows, and 29,674 glyph records. Thai glyphs are mapped through CP874 byte slots `0xA1..0xFB`; direct Unicode U+0E00..U+0E7F slots are absent. This supports CP874 bytes widened into 16-bit game character slots.
- Combining slots handled: `D1`, `D4-D7`, `D8-DA`, and `E7-EE`. `D2`, `D3`, and `E0-E6` are not classified as combining marks.
- Hooks require the exact known PE metadata and the expected 12-byte prologues. Detours overwrite only the complete first five bytes (`sub esp, 0x30; push ebx; push ebp`) and execute those bytes in a trampoline before jumping back.
- The metrics are temporary and protected by a recursive critical section. A zero-advance mark record is changed only if the 65,536-entry map does not alias that record to a spacing character. The active FNT record widths and font spacing are restored after the layout call returns.
- The supplied game executables import `DirectDrawCreate` from `DDRAW.dll`. TS-DDraw exports it at ordinal 1 and also exports its configuration data symbols. The integrated build keeps the upstream `ddraw.def` unchanged, so it preserves the renderer and its export table.
- The earlier DDraw wrapper only exposed the generic DirectDraw function set and mixed custom/system renderer targets. It is archived under `legacy-proxies` and is no longer built or installed. The version proxy only initializes the font fix when one of its forwarded version APIs is called, so the missing version log did not establish whether the hook ran.
- The current build calls `ThaiFontFixInitialize` from TS-DDraw's `DirectDrawCreate`, before `SettingsLoad` and `hook_init`. It reads `ddraw-thaifnt.ini` and writes `ddraw-thaifnt.log` when diagnostics are enabled.

## Sara Am and tone-mark stacking

### Research basis (checked 2026-10-11)

- Unicode 18.0, Chapter 16, section “Rendering of Thai Combining Marks,” says above marks are displayed outward from the base in text order. It specifically says that when U+0E33 SARA AM follows one or more tone marks (U+0E48–U+0E4B), the nikhahit that is part of Sara Am is displayed **below** those tone marks. This confirms the desired vertical order: tone mark above the Sara Am dot. [Unicode 18.0 Chapter 16](https://unicode.org/versions/Unicode18.0.0/core-spec/chapter-16/).
- The Unicode Thai character chart lists U+0E33 SARA AM with an approximate/compatibility decomposition to U+0E4D NIKHAHIT plus U+0E32 SARA AA. This explains why a single Sara Am character has an upper-dot component and a lower/right vowel component for rendering purposes. [Unicode Thai character chart](https://www.unicode.org/Public/UCD/latest/charts/nameslist/0e00/).
- These Unicode sources describe Unicode text and shaping behavior. The RA2 renderer instead receives legacy CP874 byte values widened to 16 bits and draws a precomposed bitmap glyph. The implementation therefore applies the same visual ordering to the observed game glyph; it does not add Unicode shaping or normalization.

### Game font and implementation

- In the test encoder used by `render_fnt_test.py`, U+0E48 MAI EK maps to CP874 `E8`, and U+0E33 SARA AM maps to CP874 `D3`. The sample `ต่ำ` is encoded as `B5 E8 D3`, so the tone mark is encountered immediately before Sara Am. `ดำ` (`B4 D3`) exercises Sara Am without a tone mark.
- `D3` remains a spacing glyph. `BuildSaraAmTopMask` only enables the split when the live/FNT bitmap has a compact upper component left of a long vertical run at the lower glyph's right edge. The upper pixels are drawn centered over the preceding base; the remaining Sara Aa pixels keep the game's baseline and normal Sara Am advance. A bitmap that fails these checks is left intact.
- For an accepted font state, `ThaiRenderHook` defers a tone mark for one glyph. If the next glyph is a valid `D3` Sara Am, the hook draws the split Sara Am first, then draws the pending tone mark at `ToneMarkRaise` pixels above its normal y position. Otherwise it flushes the tone mark with its ordinary placement. The debug log reports `tone mark stacked above Sara Am` when this branch runs.
- The preview harness looks ahead in the sample byte sequence to model this placement using the actual `game.fnt` bitmap records. Rebuild it with `python .\render_fnt_test.py --output .\thai-render-sara-am.png`. This checks the bitmap split and approximate coordinates, but it is not an execution of the game renderer. The current integrated DLL compiles successfully; in-game validation of this Sara Am-specific path remains a separate check.
