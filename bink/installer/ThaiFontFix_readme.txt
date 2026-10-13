ThaiFontFix for RA2 and Yuri's Revenge
By THAI RA2
https://thaira2.com

ABOUT
ThaiFontFix adjusts Thai glyph and combining-mark placement in the game's
bitmap font. Setup installs the included default game.fnt so the matching
Thai glyphs are available to the game.

INSTALLATION
1. Close Red Alert 2 and Yuri's Revenge before installing.
2. Run ThaiFontFix Setup and select the game folder containing RA2.exe,
   game.exe, ra2.mix, and language.mix.
3. Choose Install and finish setup. Start the game as usual.

The setup installs the Bink proxy and the included game.fnt. If game.fnt
already exists, setup keeps it as game.fnt.thaifnt-original. Setup creates
thaifontfix.ini only if that file does not already exist. Edit the INI to
change the font-fix options. The Bink proxy can coexist with CnCNet's
renderer selection.

FONT OPTIONS
Setup offers the current Tahoma font and these additional choices: Boiling
Point, Final War, Phobos Improved, Reborn, Rise of the East, and Westwood
Improved. The font page shows each choice's author and a rendered game-style
preview. When updating ThaiFontFix, Install applies the selected font and
keeps the original game.fnt for Uninstall.

Thai CP874 glyph bitmaps from the current Tahoma font are merged into each
additional font when Setup is built. Their original non-Thai artwork remains
unchanged, and the upstream font files are not modified.

The additional fonts are from Fonts For Red Alert 2 by mah_boi. Font credits
and the license are included as FontsForRedAlert2-README.md and
FontsForRedAlert2-LICENSE.md in the game folder. The font collection is
licensed under CC BY-NC 4.0 and is for noncommercial use only.

If ThaiFontFix is already installed, Setup shows the installed proxy version
and the version included in this setup. Choose Install to update the proxy;
Setup asks before overwriting it and keeps the original Bink backup.

UNINSTALL
Run the same setup again, select the game folder, choose Uninstall ThaiFontFix,
and finish setup. Your original Bink DLL and game.fnt are restored. If the
game folder had no game.fnt before installation, setup removes the installed
font. Settings are kept.

You can also restore the original Bink DLL manually if binkw32.bin exists:
close the game, delete binkw32.dll, and rename binkw32.bin to binkw32.dll.
