#!/usr/bin/env python3
"""
Seaglass Save Editor -- PySide6 GUI for Pokemon Emerald Seaglass saves.
Sits on top of seaglass_save.py (the tested read/write core).

Run:  python seaglass_editor.py        (Windows: use 'python' or 'py -3.12')
Deps: pip install PySide6
"""
import sys, os
from seaglass_save import SeaglassSave, Mon, NATURES, STAT_KEYS
import theme

try:
    from PySide6.QtWidgets import (
        QApplication, QMainWindow, QWidget, QSplitter, QListWidget, QListWidgetItem,
        QVBoxLayout, QHBoxLayout, QFormLayout, QGridLayout, QGroupBox, QLabel,
        QComboBox, QSpinBox, QLineEdit, QPushButton, QFileDialog, QMessageBox,
        QToolBar, QStatusBar, QCompleter)
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QAction, QFont
except ImportError:
    sys.exit("PySide6 not installed.  Run:  pip install PySide6")

STAT_LABELS = ["HP", "Atk", "Def", "Spe", "SpA", "SpD"]
DEFAULT_SAVE = "/mnt/user-data/uploads/00040000075C8E00_gbavc.sav"
DEFAULT_ROM  = "/mnt/user-data/uploads/Pokemon_Emerald_Seaglass_3_0__PokemonEmeraldseaglass_com_.gba"


def make_search_combo():
    """Editable combo with case-insensitive contains-search."""
    c = QComboBox()
    c.setEditable(True)
    c.setInsertPolicy(QComboBox.NoInsert)
    c.setMaxVisibleItems(20)
    comp = c.completer()
    comp.setCompletionMode(QCompleter.PopupCompletion)
    comp.setFilterMode(Qt.MatchContains)
    comp.setCaseSensitivity(Qt.CaseInsensitive)
    return c


class Editor(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Seaglass Save Editor")
        self.resize(980, 720)
        self.save = None
        self.rom_path = None
        self.cur = None
        self._species = []
        self._move_name2id = {}
        self._item_name2id = {}

        self._build_ui()
        if os.path.exists(DEFAULT_SAVE):
            self.load_save(DEFAULT_SAVE,
                           DEFAULT_ROM if os.path.exists(DEFAULT_ROM) else None)

    # ---------------- UI ----------------
    def _build_ui(self):
        tb = QToolBar(); self.addToolBar(tb)
        for txt, fn in [("Open Save…", self.act_open_save), ("Open ROM…", self.act_open_rom),
                        ("Save As…", self.act_save_as)]:
            a = QAction(txt, self); a.triggered.connect(fn); tb.addAction(a)
        self.setStatusBar(QStatusBar())

        root = QWidget(); root.setObjectName("root")
        rootv = QVBoxLayout(root); rootv.setContentsMargins(16, 16, 16, 12); rootv.setSpacing(12)
        header = QLabel("◈   SEAGLASS  SAVE  EDITOR"); header.setObjectName("header")
        header.setAlignment(Qt.AlignCenter)
        rootv.addWidget(header)
        split = QSplitter(); rootv.addWidget(split, 1)
        self.setCentralWidget(root)
        self.list = QListWidget(); self.list.setMinimumWidth(260)
        self.list.currentItemChanged.connect(self.on_select)
        split.addWidget(self.list)

        right = QWidget(); rl = QVBoxLayout(right); split.addWidget(right)
        split.setStretchFactor(1, 1)

        idbox = QGroupBox("Identity"); idf = QFormLayout(idbox)
        self.cb_species = make_search_combo()
        self.le_nick = QLineEdit(); self.le_nick.setMaxLength(10)
        self.cb_nature = QComboBox(); self.cb_nature.addItems(NATURES)
        self.sp_level = QSpinBox(); self.sp_level.setRange(1, 100)
        self.sp_friend = QSpinBox(); self.sp_friend.setRange(0, 255)
        self.cb_item = make_search_combo()
        idf.addRow("Species", self.cb_species)
        idf.addRow("Nickname", self.le_nick)
        idf.addRow("Nature", self.cb_nature)
        idf.addRow("Level", self.sp_level)
        idf.addRow("Friendship", self.sp_friend)
        idf.addRow("Held item", self.cb_item)
        rl.addWidget(idbox)

        self.lbl_info = QLabel(""); self.lbl_info.setObjectName("info")
        rl.addWidget(self.lbl_info)

        mvbox = QGroupBox("Moves"); mg = QGridLayout(mvbox)
        mg.addWidget(QLabel("Move"), 0, 1); mg.addWidget(QLabel("PP"), 0, 2)
        self.cb_move = []; self.sp_pp = []
        for i in range(4):
            mv = make_search_combo()
            pp = QSpinBox(); pp.setRange(0, 99)
            self.cb_move.append(mv); self.sp_pp.append(pp)
            mg.addWidget(QLabel(f"{i+1}"), i+1, 0); mg.addWidget(mv, i+1, 1); mg.addWidget(pp, i+1, 2)
        mg.setColumnStretch(1, 1)
        rl.addWidget(mvbox)

        statbox = QGroupBox("IVs (0–31)  /  EVs (0–252)"); sg = QGridLayout(statbox)
        self.sp_iv = []; self.sp_ev = []
        sg.addWidget(QLabel("IV"), 0, 0); sg.addWidget(QLabel("EV"), 0, 1)
        for c, lab in enumerate(STAT_LABELS):
            sg.addWidget(QLabel(lab), 0, c+2, alignment=Qt.AlignCenter)
        for c in range(6):
            iv = QSpinBox(); iv.setRange(0, 31)
            ev = QSpinBox(); ev.setRange(0, 252); ev.valueChanged.connect(self._ev_total)
            self.sp_iv.append(iv); self.sp_ev.append(ev)
            sg.addWidget(iv, 1, c+2); sg.addWidget(ev, 2, c+2)
        btn_maxiv = QPushButton("Max IVs"); btn_maxiv.clicked.connect(
            lambda: [w.setValue(31) for w in self.sp_iv])
        self.lbl_evtot = QLabel("EV total: 0 / 510")
        sg.addWidget(btn_maxiv, 1, 0, 1, 2); sg.addWidget(self.lbl_evtot, 2, 0, 1, 2)
        rl.addWidget(statbox)

        row = QHBoxLayout()
        self.btn_apply = QPushButton("Apply to Pokémon"); self.btn_apply.setObjectName("primary")
        self.btn_apply.clicked.connect(self.apply)
        self.btn_revert = QPushButton("Revert"); self.btn_revert.clicked.connect(self.refresh_fields)
        row.addWidget(self.btn_apply); row.addWidget(self.btn_revert)
        rl.addLayout(row); rl.addStretch(1)
        self._set_enabled(False)

    def _set_enabled(self, on):
        for w in (self.cb_species, self.le_nick, self.cb_nature, self.sp_level,
                  self.sp_friend, self.cb_item, self.btn_apply, self.btn_revert,
                  *self.cb_move, *self.sp_pp, *self.sp_iv, *self.sp_ev):
            w.setEnabled(on)

    def _ev_total(self):
        t = sum(w.value() for w in self.sp_ev)
        self.lbl_evtot.setText(f"EV total: {t} / 510" + ("  ⚠ over cap" if t > 510 else ""))

    # ---------------- load ----------------
    def act_open_save(self):
        p, _ = QFileDialog.getOpenFileName(self, "Open save", "", "Saves (*.sav);;All (*)")
        if p: self.load_save(p, self.rom_path)
    def act_open_rom(self):
        p, _ = QFileDialog.getOpenFileName(self, "Open ROM", "", "GBA (*.gba);;All (*)")
        if p and self.save: self.load_save(self.save.path, p)
        elif p: self.rom_path = p

    def _fill_combo(self, combo, pairs):
        combo.blockSignals(True); combo.clear()
        for idx, nm in pairs:
            combo.addItem(nm, idx)
        combo.blockSignals(False)

    def load_save(self, save_path, rom_path):
        try:
            self.save = SeaglassSave(save_path, rom_path)
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Could not load save:\n{e}"); return
        self.rom_path = rom_path
        self._species = self.save.species_list() if rom_path else []
        moves = self.save.move_list() if rom_path else []
        items = self.save.item_list() if rom_path else []
        self._move_name2id = {nm: i for i, nm in moves}
        self._item_name2id = {nm: i for i, nm in items}
        self._fill_combo(self.cb_species, [(i, f"{nm}  (#{i})") for i, nm in self._species])
        for mv in self.cb_move: self._fill_combo(mv, moves)
        self._fill_combo(self.cb_item, items)
        self.populate_list(); self.status()

    def status(self):
        if not self.save: return
        bad = self.save.verify_checksums()
        t = self.save.trainer()
        rom = "ROM loaded" if self.rom_path else "no ROM — load it for names/stats"
        self.statusBar().showMessage(
            f"{os.path.basename(self.save.path)} | Trainer {t['name']} | "
            f"slot {'AB'[self.save.active_slot]} | "
            f"checksums {'OK' if not bad else 'BAD '+str(bad)} | {rom}")

    # ---------------- list ----------------
    def populate_list(self):
        self.list.blockSignals(True); self.list.clear()
        h = QListWidgetItem("— PARTY —"); h.setFlags(Qt.NoItemFlags); self.list.addItem(h)
        for i, m in enumerate(self.save.party()):
            it = QListWidgetItem(f"  {i+1}. {self.save.species_name(m.species)}  "
                                 f"Lv{m.level} '{m.nickname}'")
            it.setData(Qt.UserRole, ("party", i)); self.list.addItem(it)
        h2 = QListWidgetItem("— PC BOXES —"); h2.setFlags(Qt.NoItemFlags); self.list.addItem(h2)
        for off, m in self.save.box_mons():
            it = QListWidgetItem(f"  {self.save.species_name(m.species)}  '{m.nickname}'")
            it.setData(Qt.UserRole, ("box", off)); self.list.addItem(it)
        self.list.blockSignals(False)

    def on_select(self, item, _prev=None):
        if not item: return
        d = item.data(Qt.UserRole)
        if not d: self._set_enabled(False); return
        kind, ref = d
        m = self.save.party()[ref] if kind == "party" else dict(self.save.box_mons())[ref]
        self.cur = (kind, ref, m)
        self._set_enabled(True)
        self.sp_level.setEnabled(kind == "party")
        self.refresh_fields()

    def _set_combo(self, combo, idx, fallback_text):
        i = combo.findData(idx)
        if i >= 0: combo.setCurrentIndex(i)
        else: combo.setEditText(fallback_text)

    def refresh_fields(self):
        if not self.cur: return
        kind, ref, m = self.cur
        self._set_combo(self.cb_species, m.species, f"#{m.species}")
        self.le_nick.setText(m.nickname)
        self.cb_nature.setCurrentIndex(m.nature)
        self.sp_level.setValue(m.level or 1)
        self.sp_friend.setValue(m.friendship)
        self._set_combo(self.cb_item, m.held_item, self.save.item_name(m.held_item))
        for i in range(4):
            self._set_combo(self.cb_move[i], m.moves[i], self.save.move_name(m.moves[i]))
            self.sp_pp[i].setValue(m.pp[i])
        ivs, evs = m.ivs, m.evs
        for i, k in enumerate(STAT_KEYS):
            self.sp_iv[i].setValue(ivs[k]); self.sp_ev[i].setValue(evs[k])
        self._ev_total()
        base = self.save.base_stats(m.species) if self.rom_path else [0]*6
        g = self.save.gender_of(m.pv, m.species) if self.rom_path else "?"
        self.lbl_info.setText(
            f"OT {m.ot_name}   gender {g}   {'SHINY ' if m.shiny else ''}"
            f"ability slot {m.ability_slot}\nbase stats  " +
            "  ".join(f"{l}:{v}" for l, v in zip(STAT_LABELS, base)))

    # ---------------- apply ----------------
    def _combo_id(self, combo, name2id, fallback):
        d = combo.currentData()
        if d is not None and combo.currentText() == combo.itemText(combo.currentIndex()):
            return d
        return name2id.get(combo.currentText().strip(), fallback)

    def apply(self):
        if not self.cur: return
        kind, ref, m = self.cur
        sp = self.cb_species.currentData()
        if sp is None:
            txt = self.cb_species.currentText().split("(#")[-1].rstrip(")").lstrip("#")
            sp = int(txt) if txt.isdigit() else m.species
        m.species = sp
        m.nickname = self.le_nick.text()
        m.friendship = self.sp_friend.value()
        m.held_item = self._combo_id(self.cb_item, self._item_name2id, m.held_item)
        m.moves = [self._combo_id(self.cb_move[i], self._move_name2id, m.moves[i]) for i in range(4)]
        m.pp = [w.value() for w in self.sp_pp]
        m.ivs = {k: self.sp_iv[i].value() for i, k in enumerate(STAT_KEYS)}
        m.evs = {k: self.sp_ev[i].value() for i, k in enumerate(STAT_KEYS)}
        tgt = self.cb_nature.currentIndex()
        if tgt != m.nature and self.rom_path:
            m.pv = self.save.reroll_pv(m, tgt)
        elif tgt != m.nature:
            QMessageBox.information(self, "Nature", "Load the ROM to change nature safely.")
        if kind == "party":
            self.save.set_level(m, self.sp_level.value())
            if self.rom_path: self.save.recompute_stats(m)
            self.save.write_party_mon(ref, m)
        else:
            self.save.write_box_mon(ref, m)
        bad = self.save.verify_checksums()
        self.populate_list(); self.status()
        QMessageBox.information(self, "Applied",
            f"Changes written to the in-memory save.\nChecksums: {'OK' if not bad else 'BAD '+str(bad)}\n\n"
            "Use “Save As…” to write the .sav file.")

    def act_save_as(self):
        if not self.save: return
        base = os.path.splitext(os.path.basename(self.save.path))[0]
        p, _ = QFileDialog.getSaveFileName(self, "Save As", base + "_edited.sav", "Saves (*.sav)")
        if not p: return
        if os.path.abspath(p) == os.path.abspath(self.save.path):
            if QMessageBox.question(self, "Overwrite original?",
                "This overwrites your source save. Continue?") != QMessageBox.Yes:
                return
        self.save.save_as(p)
        QMessageBox.information(self, "Saved", f"Wrote {p}")


def main():
    app = QApplication(sys.argv)
    w = Editor()
    theme.apply_theme(app, w)
    w.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
