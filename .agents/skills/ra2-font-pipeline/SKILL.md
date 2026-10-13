---
name: ra2-font-pipeline
description: Analyze the Red Alert 2 and Yuri's Revenge UI font pipeline, legacy CP874 text, FNT bitmap data, and supported x86 hook points when changing ThaiFontFix rendering.
---

# RA2 font pipeline

Use this skill for reverse engineering or modifying the game's text layout and glyph rendering path.

- Begin with the repository's [`research-notes.md`](../../../research-notes.md) and the current hook implementation in `shared/ThaiFontFix.cpp`. Treat the notes as recorded evidence; verify relevant details against the executable and source before changing a hook.
- The supported executables are 32-bit. The current notes record layout and glyph-render entry points, calling conventions, object offsets, and expected prologues. Preserve stack cleanup and executable signature checks; do not generalize a verified address to another game build.
- The game font is a `fonT` bitmap font. The renderer maps widened CP874 byte values to fixed-stride bitmap records; Thai bytes are not Unicode codepoints. Keep decoding, glyph-map indexing, and Unicode shaping as separate concepts.
- Check both the whole-string layout/direct-text path and the single-glyph render path. Cluster-aware work needs access to the full string before rendering individual glyphs.
- Use the supplied `game.fnt` and the existing preview harness for offline inspection where useful. Confirm any runtime conclusion with the actual supported game build and diagnostic log.
- Preserve the original game behavior when font state, data, or executable validation fails. Make changes in the shared implementation when behavior must be consistent across Bink, integrated DDraw, and Syringe loaders.
- The original game binaries were analyzed with Ghidra under `C:\app\reverse-tools`; use that installation or an existing analysis database when disassembly is needed.

For the TrueType/Uniscribe exploration, use [`thaifontfix-truetype`](../thaifontfix-truetype/SKILL.md) and the project [`handover.md`](../../../handover.md).
