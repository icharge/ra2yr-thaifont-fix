---
name: thaifontfix-truetype
description: Investigate or implement Thai TrueType font shaping and rendering for RA2/Yuri's Revenge UI while preserving the existing bitmap-font fix as fallback.
---

# ThaiFontFix TrueType rendering

Use this skill when exploring Windows TrueType fonts, Thai shaping, or a runtime TrueType rendering path in ThaiFontFix.

- Read the repository [`handover.md`](../../../handover.md) and [`research-notes.md`](../../../research-notes.md) first. They record the current findings, unresolved game-surface contract, and known hook behavior.
- Preserve the existing FNT bitmap renderer as the fallback. Keep experiments behind an option until runtime behavior has been verified in both RA2 and Yuri's Revenge; do not change installer payloads during an unverified prototype.
- Treat game text as CP874 bytes. Convert those bytes explicitly to Unicode before shaping; do not widen bytes and pass them as Unicode Thai codepoints. Include regression samples such as `ดำ` and `ต่ำ` plus above-vowel/tone-mark combinations.
- Start with an offline reference renderer: use Uniscribe for Thai shaping and GDI to rasterize into a controlled DIB section. Capture glyph IDs, clusters, advances, offsets, baseline, and output pixels so results can be compared with the existing FNT preview.
- Keep Windows XP compatibility in view. Prefer Uniscribe and GDI APIs available on the XP baseline; do not make DirectWrite a required dependency. Verify API availability and behavior for the exact minimum OS before relying on a feature.
- Before hooking runtime drawing, establish the game's destination pixel format/palette, transparency rule, text color, clipping, scaling, and pen-position/advance contract. A shaped glyph run is not usable until its output can be transferred to that surface correctly.
- The layout/direct-text hooks can see a complete string; the glyph renderer sees one character at a time. Shape at the string level and retain cluster placement if integrating with per-glyph calls. Avoid shaping each Thai byte independently.
- Prefer a conservative integration seam with original-render fallback if conversion, font selection, shaping, or surface validation fails. Compare a runtime path against the alternative of generating compatible FNT records offline before committing to the more invasive design.

For executable and FNT structure analysis, use [`ra2-font-pipeline`](../ra2-font-pipeline/SKILL.md).
