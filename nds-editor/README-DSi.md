# Seaglass Save Editor for DS / DSi

A graphical save editor for Pokemon Emerald Seaglass, running on DS, DS Lite,
DSi, and 3DS/2DS in DS mode through TWiLight Menu++ or a compatible homebrew loader.

The top screen shows a sprite, nickname, level, nature, ability, held item,
IVs, EVs, and moves. The touch screen has a 6-by-5 box grid and a separate
six-slot party area. Editing uses four tabs, scrollable fields, searchable
species/item/move lists, and a custom touch keyboard. Every screen is drawn
in pixels; there is no text console.

## Install

1. Copy `SeaglassSaveEditor.nds` to your SD card.
2. Put your Seaglass `.gba` and a backup copy of your `.sav` in `/roms/gba`
   or `/seaglass`. The card root is also searched, but subfolders are not
   scanned recursively.
3. Launch the app and choose the ROM, then the save. Tap a row to select it,
   then tap the Open button or press A.
4. Select a Pokemon, tap Edit, and use the tabs and controls to change it.

The ROM supplies the correct Seaglass names and sprites. If it cannot be
verified, the app shows a warning and disables the named pickers. SELECT
on a file screen shows a short ROM-name diagnostic. Continue lets you skip
the initial ROM choice or retain a previously loaded ROM.

## Controls

- D-pad: select slots or rows. Left/right moves between the box and party.
- Touch: select a slot, field, tab, list row, or button.
- A: Edit, Change, or Select, depending on the screen.
- B: back. Edits remain in memory when returning from the editor to storage.
- L/R in storage: previous/next box.
- L in the editor: next tab. Left/right or the minus/plus buttons adjust a
  value. Hold R for steps of ten.
- X: reset all unsaved Pokemon edits, after confirmation, to the values last
  loaded or successfully saved. It does not set stats to zero or undo a save.
- Y: save after confirmation, including from pickers and the keyboard.
- SELECT in a picker: search. Left/right or the arrow buttons page through lists.
- L on file screens: rescan the card. B on the first ROM screen exits;
  START or the Continue button skips ROM selection.
- START on the save-file screen: exit.

The keyboard supports touch or D-pad + A. L changes case, SELECT deletes, R
inserts a space, START accepts, and B goes back without accepting the text.
Y accepts a nickname and opens Save; X opens Reset.

Back keeps edits while you browse other Pokemon. Save writes them to the
selected file. Leaving a save with pending edits requires a discard
confirmation. A failed write keeps the unsaved indicator visible.

## Graphics and memory

The interface uses 16-bit bitmap backgrounds on both 256x192 screens.
Rounded cards, proportional pixel text, selection outlines, and touch buttons
are rendered into RAM first. Each complete screen is copied to VRAM during
its own VBlank, avoiding console scrolling and partially drawn frames.

Sprites are decoded from the ROM using the same engine as the desktop and
3DS apps. Box thumbnails are scaled front sprites, including shiny palettes,
because the shared menu-icon palette table has not been located. Up to 48
decoded sprites are cached; opening a new box fills that cache progressively.

The 16-32 MB ROM stays on the SD card. Reads use a small page cache, with
bounded scan buffers for table detection. The graphical ARM9 build uses about
1.20 MiB for linked code, data, and static buffers, including both screen
buffers, the sprite cache, and reset snapshots. That figure excludes runtime heap/stack and the
ARM7 runtime; it is not a measurement of total peak RAM. File size is separate
from working memory.

## Optional music

Copy a 16-bit stereo PCM WAV to `/seaglass/theme.wav`, at 1024-32768 Hz.
The existing `theme.wav` is already converted. For another track:

```
ffmpeg -i source.wav -ar 32768 -ac 2 -sample_fmt s16 theme.wav
```

Music is streamed in the main thread so audio interrupts do not interrupt
ROM/save FAT access. It pauses during ROM detection and resumes afterwards.

## Build and verify

Install devkitPro with `nds-dev`, then run `make` in this directory from the
devkitPro MSYS2 shell. The output is `SeaglassSaveEditor.nds`.

The C engine is shared directly with `../3ds-editor/source/seaglass_core.c`.
UI code is split into:

- `source/main.c`: DS graphics initialization, input, SD access, audio.
- `source/app.c`: screens, touch targets, editing, sprite cache.
- `source/ui.c`: portable renderer, also used by host screenshot tests.

From the repository root, with Python and host GCC installed:

```
python nds-editor/test/validate.py
```

This creates synthetic ROM/save fixtures, exercises the real UI event paths,
compares Python/C/DS-streamed names, sprite hashes and stats, checks
byte-identical save edits and section checksums, and exports renderer images
under `nds-editor/build/test/`. The preview fixtures use original geometric
test artwork, not Pokemon sprites. No private ROM/save is included.

To also check your own files without changing the originals:

```
python nds-editor/test/validate.py --save path/to/test.sav --rom path/to/game.gba
```

Build and host tests are verified locally. DS display timing, touch usability,
real Seaglass sprites, and audio still need a hardware check. The implementation
uses the bitmap background approach documented in the
[devkitPro example](https://github.com/devkitPro/nds-examples/blob/master/Graphics/Backgrounds/16bit_color_bmp/source/template.cpp).

## Current limitations

Box levels are estimated from experience. Hidden ability slot 2 is not
supported. PP is edited separately from move selection. File scanning is
limited to 64 matching files across the three supported directories.
