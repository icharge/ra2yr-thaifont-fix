# Reverse-engineering notes

- Ghidra 12.1.4 was run from `C:\app\reverse-tools` on the supplied x86 executables.
- Text layout: `game.exe` VA `0x00432490`; `gamemd.exe` VA `0x00434CD0`.
- Glyph renderer: `game.exe` VA `0x004318E0`; `gamemd.exe` VA `0x00434120`.
- Layout measures `glyph width + font spacing` and passes characters to the renderer. The renderer reads the width byte from a fixed-stride bitmap record and returns the next x coordinate.
- The layout routine uses `stdcall` and returns with `ret 0x28`; the renderer uses `thiscall` and returns with `ret 0x10`. The DLL shims preserve both stack-cleanup conventions.
- The font object points to parsed FNT data at `+0x04`; its spacing is at `+0x2c`. Parsed FNT data stores height at `+0x08`, record stride at `+0x14`, a 65,536-entry 16-bit glyph map at `+0x18`, glyph records at `+0x1c`, and unique glyph count at `+0x20`. The Thai fix validates and scans the CP874 byte slots `0x00..0xFF`, because the renderer receives those byte values widened to 16 bits.
- Supplied `game.fnt` has a 28-byte `fonT` header, 49-byte glyph records, 16 rows, and 29,674 glyph records. Thai glyphs are mapped through CP874 byte slots `0xA1..0xFB`; direct Unicode U+0E00..U+0E7F slots are absent. This supports CP874 bytes widened into 16-bit game character slots.
- Combining slots handled: `D1`, `D4-D7`, `D8-DA`, and `E7-EE`. `D2`, `D3`, and `E0-E6` are not classified as combining marks.
- Hooks require the exact known PE metadata and the expected 12-byte prologues. Detours overwrite only the complete first five bytes (`sub esp, 0x30; push ebx; push ebp`) and execute those bytes in a trampoline before jumping back.
- The metrics are temporary and protected by a recursive critical section. A zero-advance mark record is changed only if the 65,536-entry map does not alias that record to a spacing character. The active FNT record widths and font spacing are restored after the layout call returns.
- The supplied game executables import `DirectDrawCreate` from `DDRAW.dll` and Version APIs from `VERSION.dll`. Although that import table suggested an app-local `version.dll` loader might work, the game did not load that proxy reliably, so the integration was removed. The Bink proxy is the tested app-local loader and does not replace the selected renderer.
- The integrated TS-DDraw build retains all upstream exports and adds `ThaiFontFixIsActive` at ordinal 500. Syringe and the Bink proxy export the same marker. Duplicate detection invokes the marker and requires a true result; DLL names alone do not establish that ThaiFontFix is active.
- The Syringe build has separate RA2 and Yuri variants. Its handshake accepts only the recorded executable size, timestamp, and CRC; the `.dll.inj` sidecars hook the verified executable entry points before the game reaches its original code. Syringe can coexist with the Bink proxy or integrated TS-DDraw build because they share the active-hook marker.
- The earlier DDraw wrapper only exposed the generic DirectDraw function set and mixed custom/system renderer targets. It is archived under `ddraw/legacy-proxies` and is no longer built or installed.
- The TS-DDraw build calls `ThaiFontFixInitialize` from `DirectDrawCreate`, before `SettingsLoad` and `hook_init`. DDraw, Bink, and Syringe variants all read `thaifontfix.ini` and append diagnostics to `thaifontfix.log` beside their module or game directory.

## How the game executables and hook points were identified

The supplied executables were analyzed as separate 32-bit PE programs with Ghidra 12.1.4 from `C:\app\reverse-tools`. The recorded hook locations came from examining the functions' disassembly/decompiler output and cross-references, then matching the corresponding machine-code prologues in each build. Function roles are inferred from data flow and call behavior; the addresses are not symbol names supplied by the game.

The analysis followed the font object and parsed FNT pointers through the text path. The layout function reads the text sequence, looks up glyph metrics, combines glyph width with font spacing, and invokes the glyph renderer. The renderer maps the widened character value to a glyph record, reads the record's width/bitmap, draws the bitmap, and returns the next x position. The direct-text helper calls into the same font/render path. Matching these behaviors across the RA2 and Yuri executables is why these addresses are treated as the layout, glyph-render, and direct-text hooks.

The current code recognizes a build by PE32/I386 format, preferred image base, PE timestamp, and `SizeOfImage`; it then checks all 12 expected bytes at each target before installing any detour. It does not choose hook addresses from the process filename alone. The Syringe handshake applies an additional on-disk size/timestamp/CRC check.

| Analyzed executable | PE timestamp | `SizeOfImage` | Disk size / CRC checked by Syringe | Layout RVA (VA) | Glyph RVA (VA) | Direct-text RVA (VA) |
| --- | ---: | ---: | --- | ---: | ---: | ---: |
| RA2 `game.exe` | `0x3B1EBBED` | `0x743000` | `4,485,120` / `0xD33D92ED` | `0x32490` (`0x432490`) | `0x318E0` (`0x4318E0`) | `0x31CC0` (`0x431CC0`) |
| Yuri `gamemd.exe` | `0x3BDF544E` | `0x793000` | `4,813,072` / `0x54CC0A13` | `0x34CD0` (`0x434CD0`) | `0x34120` (`0x434120`) | `0x34500` (`0x434500`) |

The Syringe sizes and CRCs above are enforced in `shared/ThaiFontFix.cpp`; the loader-side hook selection uses timestamp and image size. Treat a new executable as unsupported until its PE identity, function behavior, calling conventions, and target signatures have been analyzed and added explicitly.

### Hook installation and calling conventions

- The glyph renderer is `thiscall` and returns with `ret 0x10`. Its shim preserves the `this` pointer, code, x/y, color, and integer return value.
- The layout function is `stdcall` and returns with `ret 0x28`; the shim preserves the font, target, text, position, dimensions, flags, and return value.
- The direct-text helper is `thiscall`. The hook uses an x86 `__fastcall` shim with a dummy second register argument so it can receive the original `ECX` object pointer and preserve the original stack arguments.
- `InstallFontHooks` first checks the PE identity and these 12-byte signatures:
  - renderer: `83 EC 30 53 55 56 57 8B 7C 24 44 8B`
  - layout: `83 EC 30 53 55 56 8B 74 24 40 57 33`
  - direct text: `83 EC 10 8B 54 24 14 53 55 56 66 8B`
- Each detour replaces five bytes with a relative `E9` jump. Those five bytes are complete instructions for the renderer and layout targets. The direct-text trampoline copies seven bytes because its first instructions are a three-byte `sub esp, 0x10` and a four-byte `mov edx, [esp+0x14]`.
- A trampoline executes the copied original instructions and jumps back after them. The installer changes page protection only while writing, flushes the instruction cache, and restores the original protection. If installing a later hook fails, earlier overwritten entry bytes are restored.
- The layout and direct-text shims create a nested render context under a recursive critical section, call the original function through its trampoline, then restore modified metrics and unwind the context. The glyph hook only applies Thai placement when it finds the matching active context.

In a runtime session, `font hooks installed` confirms the PE/signature checks passed and entry detours were installed. It does not prove a font was accepted or that Thai text reached the hooks. Open a UI that draws Thai and check `thaifontfix.log` for `layout hook reached`, `glyph render hook reached` or `direct text hook reached`, `FNT accepted`, `metrics applied`, and a Thai mark/Sara Am handling message.

## How the bitmap font is structured and modified

The game renders `game.fnt` bitmaps rather than Windows TrueType text. The supplied file uses a 28-byte `fonT` header, followed by a 65,536-entry little-endian 16-bit glyph map (`0x20000` bytes), followed by fixed-stride glyph records. Header offsets observed by the parser are: signature at `+0x00`, height at `+0x0C`, glyph count at `+0x14`, and record stride at `+0x18`. In the supplied font, height is 16, stride is 49 bytes, and the file contains 29,674 records. Each record begins with its width byte and then 16 rows of three bitmap bytes (MSB-first pixels). The Thai CP874 bytes `0xA1`–`0xFB` map to records; the font does not directly map Unicode U+0E00–U+0E7F.

For a bitmap-font change, preserve the `fonT` structure and CP874 byte-to-record map, edit the intended record bitmap and width, then render representative CP874 strings with `render_fnt_test.py`. Map entries are 16-bit little-endian and one-based (zero means unmapped). The renderer uses the width byte as the glyph advance input, with font spacing added by layout. Thai marks that must overlay a base need zero advance and correct bitmap artwork/placement. Sara Am (`0xD3`) is normally a spacing glyph and is split by the runtime fix only when its bitmap passes the compact-dot/right-stroke checks.

The repository currently has a preview script, not a general-purpose FNT editor or TrueType-to-FNT converter. `render_fnt_test.py` reads `game.fnt` from the repository root and currently has no input-font option. For a new font asset, preserve an untouched copy, implement/use an editor or converter that writes the documented format, and extend the preview tool to accept the candidate path before treating its output as evidence.

The runtime fix does **not** edit `game.exe`, the on-disk `game.fnt`, or the game's original parsed font records. It validates the live font object and glyph map, makes a private copy of parsed data and bitmap records, and points the font object at that private copy. During a layout call, it temporarily sets combining-mark widths to zero, moves the original spacing into spacing glyph advances when there is enough bitmap padding, and sets font spacing to zero; it restores all widths and spacing after the call. Mark drawing and the Sara Am split operate on that private copy and restore temporary bitmap changes after drawing.

If live bitmap records cannot be safely copied, `game.fnt` beside the DLL may provide fallback records. The fallback is deliberately strict: its signature, height, stride, usable file length, and all 256 low-byte glyph-map entries must match the live font; every mapped byte must point within the records being copied. It copies the records present in both files and zero-fills the remainder of the private runtime allocation. It is not a general-purpose way to replace a font with a different map or dimensions. The observed runtime table had 34,113 records while the supplied file had 29,674; the fallback succeeded because the CP874 entries used by the game matched and fell within the copied records.

The preview harness in `render_fnt_test.py` reads actual `game.fnt` records and models the Thai layout adjustments, including Sara Am. It is an offline bitmap/layout preview, not the game's actual renderer. Runtime correctness still requires testing in the supported executable and checking diagnostics/screenshots.

## TrueType rendering investigation status

The bitmap path remains the compatibility fallback. A Bink-only, opt-in live TrueType prototype now decodes complete CP874 strings, uses Uniscribe/GDI to rasterize them, and overlays the result on the game's 16-bit surface from the final glyph callback. This placement keeps surface access inside the original text draw. The prototype retains FNT measurement and wrapping, and declines unsupported control/multicolor runs or uncertain surface bounds. It is not visually verified; test results and remaining limitations are tracked in [`handover.md`](handover.md).

The glyph hook alone is too late to shape Thai clusters because it receives one byte at a time. The prototype pairs the full string and computed glyph positions from the layout/direct-text context. The static renderer contract, including packed foreground color and clipping, is described below. Offline TrueType-to-FNT generation remains an alternative if runtime surface access proves unreliable.

## Sara Am and tone-mark stacking

### Research basis (checked 2026-10-11)

- Unicode 18.0, Chapter 16, section “Rendering of Thai Combining Marks,” says above marks are displayed outward from the base in text order. It specifically says that when U+0E33 SARA AM follows one or more tone marks (U+0E48–U+0E4B), the nikhahit that is part of Sara Am is displayed **below** those tone marks. This confirms the desired vertical order: tone mark above the Sara Am dot. [Unicode 18.0 Chapter 16](https://unicode.org/versions/Unicode18.0.0/core-spec/chapter-16/).
- The Unicode Thai character chart lists U+0E33 SARA AM with an approximate/compatibility decomposition to U+0E4D NIKHAHIT plus U+0E32 SARA AA. This explains why a single Sara Am character has an upper-dot component and a lower/right vowel component for rendering purposes. [Unicode Thai character chart](https://www.unicode.org/Public/UCD/latest/charts/nameslist/0e00/).
- These Unicode sources describe Unicode text and shaping behavior. The RA2 renderer instead receives legacy CP874 byte values widened to 16 bits and draws a precomposed bitmap glyph. The implementation therefore applies the same visual ordering to the observed game glyph; it does not add Unicode shaping or normalization.

### Game font and implementation

- In the test encoder used by `render_fnt_test.py`, U+0E48 MAI EK maps to CP874 `E8`, and U+0E33 SARA AM maps to CP874 `D3`. The sample `ต่ำ` is encoded as `B5 E8 D3`, so the tone mark is encountered immediately before Sara Am. `ดำ` (`B4 D3`) exercises Sara Am without a tone mark.
- `D3` remains a spacing glyph. `BuildSaraAmTopMask` only enables the split when the live/FNT bitmap has a compact upper component left of a long vertical run at the lower glyph's right edge. The upper pixels are drawn centered over the preceding base; the remaining Sara Aa pixels keep the game's baseline and normal Sara Am advance. A bitmap that fails these checks is left intact.
- For an accepted font state, `ThaiRenderHook` defers a tone mark for one glyph. If the next glyph is a valid `D3` Sara Am, the hook draws the split Sara Am first, then draws the pending tone mark at `ToneMarkRaise` pixels above its normal y position. Otherwise it flushes the tone mark with its ordinary placement. The debug log reports `tone mark stacked above Sara Am` when this branch runs.
- The preview harness looks ahead in the sample byte sequence to model this placement using the actual `game.fnt` bitmap records. Rebuild it with `python .\render_fnt_test.py --output .\thai-render-sara-am.png`. This checks the bitmap split and approximate coordinates, but it is not an execution of the game renderer. The Bink, integrated DDraw, and Syringe builds compile; confirm Sara Am-specific behavior in game as well.

## Game glyph-render surface contract (static analysis, 2026-10-13)

Both installed supported executables were checked against the hook identities and analyzed with Ghidra 12.1.4. The exact files were RA2 `game.exe` (SHA-256 `DA34AD390DED45AC738A94707F05DA452502F9602537AAA2DC8BE50BAA091098`, timestamp `0x3B1EBBED`, image size `0x743000`) and Yuri's Revenge `GameMD.exe` (SHA-256 `7B8A068535D6AF06845EDF95AE829B113D00C02909330E16F197426CD7DB94B6`, timestamp `0x3BDF544E`, image size `0x793000`). All 12-byte prologues already recorded for the layout, glyph-render, and direct-text hooks match these files.

The two builds have the same relevant rendering contract, with these entry points:

| Function | RA2 `game.exe` | Yuri's Revenge `GameMD.exe` |
| --- | ---: | ---: |
| Layout | `0x00432490` | `0x00434CD0` |
| Glyph renderer | `0x004318E0` | `0x00434120` |
| Direct-text helper | `0x00431CC0` | `0x00434500` |
| Clip intersection helper | `0x00432000` | `0x00434840` |

Static observations from the glyph renderer and its helpers:

- The layout and direct-text paths receive the complete NUL-terminated `short*` string, walk its 16-bit units, and call the glyph renderer for each unit. There is no shaping step. The next pen x comes from the renderer's return value.
- The renderer maps each character through the parsed FNT glyph map, reads the glyph width from the record, and draws its 1-bit rows. Set bits overwrite destination pixels; unset bits leave the existing surface unchanged.
- The renderer writes through a `ushort*` at the font object's `+0x0C` field. Its row stride is stored in `+0x10` as the surface pitch divided by two, so the text target is a 16-bit-per-pixel buffer. A set glyph bit writes one 16-bit color word. The color defaults to the font object's `+0x24` word and uses the per-glyph color argument when that argument is not `-1`.
- Before drawing a text run, the game calls virtual methods on the target object: slot `+0x5C` supplies the pixel pointer, slot `+0x74` supplies a byte pitch that the game divides by two, and slots `+0x7C`/`+0x80` provide bounds used to initialize the clip rectangle. After the run, slot `+0x60` releases the target and the game clears its cached pointer and pitch. These method roles are inferred from their results and use; the target wrapper's concrete type and method names are not established.
- With clipping enabled (`font + 0x41`), each glyph rectangle from `(x, y)` through `(x + width - 1, y + height - 1)` is intersected with the font clip rectangle at `+0x30`. The intersection helper uses inclusive edges, and the renderer checks each set pixel against those bounds.
- A normal glyph returns `x + glyph width + font spacing` (`font + 0x2C`). The layout also handles tabs, line breaks, wrapping, alignment, and per-glyph color changes before calling the renderer.

Primary-surface setup at `0x004BA770` queries a `DDSURFACEDESC`, saves the returned RGB bit masks, and derives per-channel shifts used by the game's color-packing helper at `0x004355D0`. The text renderer therefore receives an already packed 16-bit color word; the conversion is based on the primary surface's reported masks instead of one hard-coded RGB layout. Static code checks several packed 15/16-bit layouts, but it does not reveal which mask values were returned in the prior Yuri run. The target wrapper's concrete type and exact lock semantics also remain unresolved. The installed `ddraw.ini` selects `Renderer=auto`; that setting alone does not show which presentation renderer was active. Existing `thaifontfix.log` entries confirm that Yuri's Revenge reached the direct-text, layout, and glyph hooks, but they do not record the surface masks or presentation path. A TrueType renderer will need to convert its GDI output using the active masks, preserve the existing transparent-bit and clip behavior, and use the same locked destination buffer.
