# Seaglass Save Editor

A little save editor for **Pokémon Emerald Seaglass**. PKHeX and the usual tools don't understand Seaglass — it's built on pokeemerald-expansion, which renumbers all the species, moves, and items — so I made one that does.


## Get it

Download **SeaglassSaveEditor.exe** from here as a zip and hten unzip and run the exe in the /dist folder.

> Windows might show an "unknown publisher" warning the first time — it's just an unsigned hobby app, click **More info → Run anyway**.

## What you'll need

- The editor (above)
- Your Seaglass **`.gba`** ROM
- Your **`.sav`** file

The ROM isn't optional — it's where the Pokémon/move/item names come from and it keeps your edits valid. No ROM is included; bring your own.

## How to use it

1. **Open Save** — pick your `.sav`
2. **Open ROM** — pick your Seaglass `.gba`
3. Click a Pokémon in the list on the left
4. Change whatever you want → **Apply to Pokémon**
5. **Save As** — writes a new file and leaves your original untouched

You can edit species, nickname, nature, ability, gender, shininess, level, friendship, held item, all four moves, and IVs/EVs. Stats are recalculated automatically, and flipping nature/gender/shiny won't scramble the other two.

## Getting your save back onto a 3DS (GBA Virtual Console)

If you play Seaglass as a VC game on a hacked 3DS/2DS, the save lives inside the game's 3DS data, so you go through GodMode9:

1. Dump the save in GodMode9 → you get a `..._gbavc.sav`
2. Edit it with this tool
3. Inject the edited file back into the **same game** in GodMode9 (it fixes the checksum the VC needs — a plain copy won't boot)
4. **Back up the original first**, then load it and check your team

If you run the ROM through open_agb_firm instead, the save is just a plain `.sav` in `/3ds/open_agb_firm/saves/` — swap the file and you're done.

## Heads up

- **Back up your save before editing.** Always.
- For use with your own copy of the game. Not affiliated with Nintendo or the Seaglass dev, and no ROM is included.

## Run from source (optional)

```
pip install PySide6
python seaglass_editor.py
```

Python 3.9+. Keep `seaglass_editor.py`, `seaglass_save.py`, and `theme.py` together.

## Thanks

- **Pokémon Emerald Seaglass** by Nemo622 — https://ko-fi.com/nemo622
- Built on [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion)

