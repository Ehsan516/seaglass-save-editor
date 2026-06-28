# Seaglass Save Editor

A save editor for **Pokémon Emerald Seaglass** (built on pokeemerald-expansion),
since PKHeX doesn't support it.

## Files
- `seaglass_save.py` — read/write engine (no GUI; importable/scriptable).
- `seaglass_editor.py` — PySide6 GUI on top of the engine.

## Run
```
pip install PySide6
python3 seaglass_editor.py
```
It auto-loads a save/ROM if found at the default paths; otherwise use
**Open Save…** and **Open ROM…**. The ROM is needed for species names,
base stats, and gender data (i.e. for safe level/nature/species edits).

## What you can edit so far
Species, nickname, nature, level, friendship, held item (by name), 4 moves + PP (by name),
all 6 IVs, all 6 EVs. Party stats are **recomputed automatically** on save so
the game won't revert an edited level, and changing nature **rerolls the
personality value while preserving shininess, gender, and ability slot**.

Workflow: pick a Pokémon → edit fields → **Apply to Pokémon** → **Save As…**
(defaults to a *new* `*_edited.sav`, never your original).

## Getting the edited save back onto your device (GBA VC)
Dumpd the save from the VC title with GodMode9. To put the edited one back:
1. Copy `*_edited.sav` to your SD card.
2. In GodMode9, **inject** it into the same GBA VC title's save (the
   reverse of how you dumped). GodMode9 recomputes the save's CMAC, which the
   VC wrapper requires — a raw byte-swap won't boot without it.
3. **Keep your original dump as a backup** so you can always roll back.
4. Boot the game and check your party.

(If you ever switch this ROM to **open_agb_firm**, its save in
`/3ds/open_agb_firm/saves/` is a raw `.sav` — then editing is just swap-the-file,
no CMAC step.)

## Current limitations / notes
- **Move & held-item pickers are now name-based** and searchable (type to filter,
  e.g. "rock" or "leftovers"). 935 moves / 1027 items resolved from the ROM.
- **Ability names** not shown yet (slot 0/1 is shown and preserved).
- **Species change across different growth-rate groups**: exp is set from the
  Pokémon's pre-change growth group. Fine within a line; for a cross-group swap,
  set the level after applying the species change once.
- Edits the **active save slot** only and leaves the save counter alone — the
  game loads your edited slot on next boot.

## Verified facts baked in
- National-Dex species indexing; party at SaveBlock1+0x238.
- Standard Gen-3 crypto (key = PV^OTID, order = PV%24, 24×u16 checksum).
- ROM SpeciesInfo stride 0xD0: name @struct+0x2c, base stats @struct+0x00
  (HP/Atk/Def/Spe/SpA/SpD), gender ratio @struct+0x12.
- Move names via gMovesInfo name pointers (field @0x6d2a18, stride 0x38).
- Item names embedded @0x67e77c, stride 0x54, indexed by item ID
  (expansion order: 1=Poké Ball, 2=Great, 3=Ultra, 4=Master, …).
