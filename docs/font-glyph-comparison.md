# Special Unicode Glyphs in the RA2 FNT Fonts

Comparison of U+231A, U+26A1, U+26CF, and U+26DF in the ThaiFontFix font sources against the extracted CnCNet font. Each listed font now renders the reference bitmap for all four symbols.

## Glyphs

| Code point | Character | Unicode name |
|---|---|---|
| U+231A | ⌚ | WATCH |
| U+26A1 | ⚡ | HIGH VOLTAGE SIGN |
| U+26CF | ⛏ | PICK |
| U+26DF | ⛟ | BLACK TRUCK |

Unicode names: [Miscellaneous Technical and other symbols](https://www.unicode.org/Public/18.0.0/charts/nameslist/2300/) and [Miscellaneous Symbols](https://www.unicode.org/Public/18.0.0/charts/nameslist/2600/).

## Comparison

The reference is `cncnet-mix/cncnet.fnt` (16-pixel glyph height, 49-byte glyph records). Its four mapped glyph bitmaps were compared with decoded glyph records in each source font. Reborn uses 33-byte records; its rows were expanded for comparison. “Same” means the glyph bitmap pixels match the reference, not that the glyph record index matches.

| Font file | U+231A | U+26A1 | U+26CF | U+26DF |
|---|---|---|---|---|
| Installer default: `thai-font-fix/bink/installer/assets/fonts/default/game.fnt` | Same | Same | Same | Same |
| Boiling Point | Same | Same | Same | Missing |
| Final War | Different | Different | Same | Missing |
| Phobos Improved | Same | Same | Same | Same |
| Reborn | Same | Same | Same | Same |
| Rise of the East | Same | Same | Same | Same |
| Tahoma 8px source | Same | Same | Same | Same |
| Westwood Improved | Same | Same | Same | Missing |

Font source files are under `thai-font-fix/fonts/FontsForRedAlert2/Source/<font>/game.fnt`.

## CnCNet reference note

The extracted font `cncnet-mix/cncnet.fnt` contains all four glyph mappings. The separate root-level `cncnet.fnt` contains U+231A, U+26A1, and U+26CF, but not U+26DF; use the extracted font as the four-glyph reference.

## Installer packaging

`thai-font-fix/bink/installer/build-installer.ps1` creates the installer default by combining WavePY's Tahoma 8 px source with the Thai CP874 glyph artwork in `thai-font-fix/bink/installer/assets/fonts/thai-glyphs/game.fnt` by THAI RA2 (iCharge). It then merges Thai CP874 glyphs from that combined Tahoma into six selectable variants: Boiling Point, Final War, Phobos Improved, Reborn, Rise of the East, and Westwood Improved. The build validates the four symbol mappings in every font and reads the source FNTs without changing them.

The generated default preserves Tahoma's non-Thai artwork and mappings, adds iCharge's Thai glyphs, and retains the four reference symbols.

## Porting considerations

- All installer fonts and source variants currently have the same artwork as the extracted CnCNet reference for all four symbols. Reborn's 33-byte records were expanded to 49-byte records for the comparison.
- Adding a mapping makes the bitmap addressable by the game; this comparison does not establish that the game gives these symbols any special semantic behavior.
