# Seaglass Save Editor — 3DS homebrew

Edit your Pokémon Emerald Seaglass save **directly on your 3DS/2DS** — no more
pulling the SD card out to use the desktop editor. Same engine as the desktop
app (checksums, Pokémon crypto, ROM auto-detection), rebuilt in C for the console.

**PKSM-style dual-screen UI:** the top screen shows a live summary of the
selected Pokémon (sprite, IV/EV table, moves); the bottom screen is your party
strip plus all 14 PC boxes as a touchable grid. Sprites are decoded straight
from the ROM, shinies included.

## What you can edit
Species, nickname (touch keyboard), level (exp-curve aware, stats recomputed),
nature, gender, shiny, ability slot, friendship, held item, all four moves,
IVs (with a max-all button) and EVs. Party and PC-box Pokémon both work.

## How it works (no PC in the loop)

GBA Virtual Console saves are CMAC-signed, so the safe way in and out is
GodMode9 — which you already use:

1. **GodMode9** → dump your GBA VC save (it lands in `sdmc:/gm9/out`)
2. Open **Seaglass Save Editor** from the Homebrew Launcher → it finds the
   dump automatically → edit → press **START** to save
3. **GodMode9** → inject the edited `.sav` back into the title
   (GM9 re-signs the CMAC for you)

The app also works on plain `.sav`/`.srm` files anywhere on the SD card
(e.g. RetroArch saves), same as the desktop version.

## Setup

1. Put your Seaglass ROM at **`sdmc:/3ds/seaglass/rom.gba`**
   (the app needs it for names, stats and version auto-detection —
   it also scans `/roms/gba` and `/gm9/out` if that file isn't there)
2. Copy `SeaglassSaveEditor.3dsx` to `sdmc:/3ds/`
3. Launch from the Homebrew Launcher

## Controls

Selecting a **move**, **item** or **species** opens a full alphabetical list on
the top screen — page through it with **L/R** (or Left/Right), pick with **A**.
The **STATS** page shows current stat totals up top (updating live, with nature
arrows) while you edit IVs/EVs on the bottom, so you can see exactly what each
change does.

Everything is touchable (tap a Pokémon to select, tap again to edit; tap the
`-` `+` fields to change values; on-screen STATS / MAX IV / SAVE / BACK
buttons). The D-pad works everywhere too:

| Key | Action |
|-----|--------|
| D-pad | move around the grid / between fields |
| L / R | previous / next box |
| A | select / edit (nickname opens the keyboard) |
| Left/Right in editor | change value (hold **R** for ×10) |
| L/R in picker | previous / next page |
| Y | max all IVs |
| START | write the save file |
| X (browser) | write the save file |
| B | back |

## UI

Top screen: the selected Pokémon's front sprite (shiny-aware, straight from
the ROM), name, level, nature/ability/item and an IV/EV panel — in the same
emerald theme as the desktop app. Bottom screen: the field editor.
Extra keys: **L / ZL-ZR** pick which stat the IV/EV rows edit.

## Building

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the
`3ds-dev` group, then:

```
make
```

That produces `SeaglassSaveEditor.3dsx` (Homebrew Launcher) **and**
`SeaglassSaveEditor.elf`. For a **CIA** you can install to the home menu:

1. Download `bannertool.exe` (github.com/Steveice10/bannertool/releases) and
   `makerom.exe` (github.com/3DSGuy/Project_CTR/releases) into this folder
2. Double-click **`build-cia.bat`**
3. Copy `SeaglassSaveEditor.cia` to your SD card and install it with **FBI**

Icon, banner, sound and the `.rsf` spec are already in `meta/`.

> The save engine (`source/seaglass_core.c`) is plain C99 with no 3DS
> dependencies — it compiles on a PC too, and is byte-for-byte validated
> against the desktop Python editor.

## Notes

- Old-2DS/3DS: the 16 MB ROM is loaded into RAM; fine under the Homebrew
  Launcher. If loading ever fails, close background apps or use a CIA build.
- If you see "ROM not recognised", the `.gba` you supplied isn't a Seaglass
  build the auto-detection understands — make sure it's the same version your
  save was created on.
