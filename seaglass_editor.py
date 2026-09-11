#!/usr/bin/env python3
"""
Seaglass Save Editor -- PySide6 GUI for Pokemon Emerald Seaglass saves.
Sits on top of seaglass_save.py (the tested read/write core).

Run:  python seaglass_editor.py        (Windows: use 'python' or 'py -3.12')
Deps: pip install PySide6
"""
import sys, os
from seaglass_save import (SeaglassSave, Mon, NATURES, STAT_KEYS, CONTEST_KEYS,
                            BALL_NAMES, ORIGIN_GAME_NAMES)
import theme

try:
    from PySide6.QtWidgets import (
        QApplication, QMainWindow, QWidget, QSplitter, QListWidget, QListWidgetItem,
        QVBoxLayout, QHBoxLayout, QFormLayout, QGridLayout, QGroupBox, QLabel,
        QComboBox, QSpinBox, QLineEdit, QPushButton, QFileDialog, QMessageBox,
        QToolBar, QStatusBar, QCompleter, QCheckBox, QScrollArea, QFrame, QSizePolicy)
    from PySide6.QtCore import Qt, QEvent, QSize
    from PySide6.QtGui import QAction, QFont, QImage, QPixmap, QIcon
except ImportError:
    sys.exit("PySide6 not installed.  Run:  pip install PySide6")

STAT_LABELS = ["HP", "Atk", "Def", "Spe", "SpA", "SpD"]
DEFAULT_SAVE = "/mnt/user-data/uploads/00040000075C8E00_gbavc.sav"
DEFAULT_ROM  = "/mnt/user-data/uploads/Pokemon_Emerald_Seaglass_3_0__PokemonEmeraldseaglass_com_.gba"


class NoHoverWheelMixin:
    """Ignores mouse-wheel scrolling unless the widget is focused (clicked/tabbed
    into first). Without this, Qt changes a spin box or combo's value just from
    scrolling the panel with the cursor resting over it, which is surprising and
    easy to trigger by accident. Focus must come from a click or Tab, not the
    wheel itself, so set focusPolicy to StrongFocus rather than the Qt default."""
    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)
        self.setFocusPolicy(Qt.StrongFocus)

    def wheelEvent(self, event):
        if self.hasFocus():
            super().wheelEvent(event)
        else:
            event.ignore()


class WheelSafeSpinBox(NoHoverWheelMixin, QSpinBox):
    pass


class WheelSafeComboBox(NoHoverWheelMixin, QComboBox):
    pass


class SearchableComboBox(NoHoverWheelMixin, QComboBox):
    """Editable combo that pops its list open on click and filters as you type."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setEditable(True)
        self.setInsertPolicy(QComboBox.NoInsert)
        self.setMaxVisibleItems(18)
        c = self.completer()
        c.setCompletionMode(QCompleter.PopupCompletion)
        c.setFilterMode(Qt.MatchContains)
        c.setCaseSensitivity(Qt.CaseInsensitive)
        self.lineEdit().installEventFilter(self)

    def eventFilter(self, obj, ev):
        if obj is self.lineEdit() and ev.type() == QEvent.MouseButtonPress and self.isEnabled():
            self.showPopup()
            return True
        return super().eventFilter(obj, ev)


class Editor(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Seaglass Save Editor")
        self.resize(1040, 760)
        self.save = None
        self.rom_path = None
        self.cur = None
        self._species = []
        self._move_name2id = {}
        self._item_name2id = {}
        self._icon_cache = {}
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
        spacer = QWidget(); spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        tb.addWidget(spacer)
        self.theme_name = theme.DEFAULT_THEME
        self.btn_theme = QPushButton(self._other_theme_label())
        self.btn_theme.clicked.connect(self.toggle_theme)
        tb.addWidget(self.btn_theme)
        self.setStatusBar(QStatusBar())

        root = QWidget(); root.setObjectName("root")
        rootv = QVBoxLayout(root); rootv.setContentsMargins(16, 16, 16, 12); rootv.setSpacing(12)
        header = QLabel("\u25c8   SEAGLASS  SAVE  EDITOR"); header.setObjectName("header")
        header.setAlignment(Qt.AlignCenter)
        rootv.addWidget(header)
        split = QSplitter(); rootv.addWidget(split, 1)
        self.setCentralWidget(root)

        self.list = QListWidget(); self.list.setMinimumWidth(250)
        self.list.setIconSize(QSize(34, 34))
        self.list.currentItemChanged.connect(self.on_select)
        split.addWidget(self.list)

        # right editor panel, width-capped and centered so it never stretches thin
        right = QWidget(); right.setObjectName("editorPanel"); right.setMaximumWidth(860)
        rl = QVBoxLayout(right); rl.setSpacing(12)
        wrap = QWidget(); wl = QHBoxLayout(wrap); wl.setContentsMargins(0, 0, 0, 0)
        wl.addStretch(1); wl.addWidget(right); wl.addStretch(1)
        scroll = QScrollArea(); scroll.setObjectName("editorScroll")
        scroll.setWidgetResizable(True); scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll.setWidget(wrap); scroll.viewport().setAutoFillBackground(False)
        wrap.setAutoFillBackground(False)
        split.addWidget(scroll)
        split.setStretchFactor(1, 1); split.setSizes([260, 780])

        self.sprite_label = QLabel(); self.sprite_label.setObjectName("sprite")
        self.sprite_label.setAlignment(Qt.AlignCenter); self.sprite_label.setFixedHeight(150)
        rl.addWidget(self.sprite_label)

        # --- Identity ---
        idbox = QGroupBox("Identity"); idf = QFormLayout(idbox)
        idf.setSpacing(8)
        self.cb_species = SearchableComboBox()
        self.le_nick = QLineEdit(); self.le_nick.setMaxLength(10)
        self.cb_nature = WheelSafeComboBox(); self.cb_nature.addItems(NATURES)
        self.cb_ability = WheelSafeComboBox()
        self.cb_gender = WheelSafeComboBox()
        self.chk_shiny = QCheckBox("Shiny")
        self.sp_level = WheelSafeSpinBox(); self.sp_level.setRange(1, 100)
        self.sp_friend = WheelSafeSpinBox(); self.sp_friend.setRange(0, 255)
        self.cb_item = SearchableComboBox()
        idf.addRow("Species", self.cb_species)
        idf.addRow("Nickname", self.le_nick)
        idf.addRow("Nature", self.cb_nature)
        idf.addRow("Ability", self.cb_ability)
        idf.addRow("Gender", self.cb_gender)
        idf.addRow("", self.chk_shiny)
        idf.addRow("Level", self.sp_level)
        idf.addRow("Friendship", self.sp_friend)
        idf.addRow("Held item", self.cb_item)
        rl.addWidget(idbox)

        self.lbl_info = QLabel(""); self.lbl_info.setObjectName("info")
        rl.addWidget(self.lbl_info)

        # --- Moves ---
        mvbox = QGroupBox("Moves"); mg = QGridLayout(mvbox)
        mg.addWidget(QLabel("Move"), 0, 1); mg.addWidget(QLabel("PP"), 0, 2)
        mg.addWidget(QLabel("PP Up"), 0, 3)
        self.cb_move = []; self.sp_pp = []; self.sp_ppup = []
        for i in range(4):
            mv = SearchableComboBox()
            pp = WheelSafeSpinBox(); pp.setRange(0, 99); pp.setMaximumWidth(80)
            ppup = WheelSafeSpinBox(); ppup.setRange(0, 3); ppup.setMaximumWidth(60)
            self.cb_move.append(mv); self.sp_pp.append(pp); self.sp_ppup.append(ppup)
            mg.addWidget(QLabel(f"{i+1}"), i+1, 0); mg.addWidget(mv, i+1, 1)
            mg.addWidget(pp, i+1, 2); mg.addWidget(ppup, i+1, 3)
        mg.setColumnStretch(1, 1)
        rl.addWidget(mvbox)

        # --- IVs / EVs ---
        statbox = QGroupBox("IVs (0–31)  /  EVs (0–252)"); sg = QGridLayout(statbox)
        self.sp_iv = []; self.sp_ev = []
        sg.addWidget(QLabel("IV"), 0, 0); sg.addWidget(QLabel("EV"), 0, 1)
        for c, lab in enumerate(STAT_LABELS):
            sg.addWidget(QLabel(lab), 0, c+2, alignment=Qt.AlignCenter)
        for c in range(6):
            iv = WheelSafeSpinBox(); iv.setRange(0, 31)
            ev = WheelSafeSpinBox(); ev.setRange(0, 252); ev.valueChanged.connect(self._ev_total)
            self.sp_iv.append(iv); self.sp_ev.append(ev)
            sg.addWidget(iv, 1, c+2); sg.addWidget(ev, 2, c+2)
        btn_maxiv = QPushButton("Max IVs"); btn_maxiv.clicked.connect(
            lambda: [w.setValue(31) for w in self.sp_iv])
        self.lbl_evtot = QLabel("EV total: 0 / 510")
        sg.addWidget(btn_maxiv, 1, 0, 1, 2); sg.addWidget(self.lbl_evtot, 2, 0, 1, 2)
        rl.addWidget(statbox)

        # --- Contest stats ---
        constbox = QGroupBox("Contest Stats (0–255)"); cg = QGridLayout(constbox)
        self.sp_contest = {}
        for c, key in enumerate(CONTEST_KEYS):
            cg.addWidget(QLabel(key.capitalize()), 0, c, alignment=Qt.AlignCenter)
            sp = WheelSafeSpinBox(); sp.setRange(0, 255)
            self.sp_contest[key] = sp
            cg.addWidget(sp, 1, c)
        rl.addWidget(constbox)

        # --- Origin & status ---
        obox = QGroupBox("Origin & Status"); of = QFormLayout(obox)
        of.setSpacing(8)
        prow = QHBoxLayout()
        self.sp_pokerus_strain = WheelSafeSpinBox(); self.sp_pokerus_strain.setRange(0, 15)
        self.sp_pokerus_days = WheelSafeSpinBox(); self.sp_pokerus_days.setRange(0, 15)
        prow.addWidget(QLabel("Strain")); prow.addWidget(self.sp_pokerus_strain)
        prow.addWidget(QLabel("Days left")); prow.addWidget(self.sp_pokerus_days)
        prow.addStretch(1)
        pwrap = QWidget(); pwrap.setLayout(prow)
        of.addRow("Pokérus", pwrap)
        self.sp_met_level = WheelSafeSpinBox(); self.sp_met_level.setRange(0, 100)
        of.addRow("Met level", self.sp_met_level)
        self.sp_met_location = WheelSafeSpinBox(); self.sp_met_location.setRange(0, 255)
        self.sp_met_location.setToolTip("Raw location-table index; not decoded to a name yet.")
        of.addRow("Met location #", self.sp_met_location)
        self.cb_origin_game = WheelSafeComboBox()
        for i, nm in enumerate(ORIGIN_GAME_NAMES): self.cb_origin_game.addItem(nm, i)
        of.addRow("Origin game", self.cb_origin_game)
        self.cb_ball = WheelSafeComboBox()
        for i, nm in enumerate(BALL_NAMES): self.cb_ball.addItem(nm, i)
        of.addRow("Caught in", self.cb_ball)
        self.cb_ot_gender = WheelSafeComboBox()
        self.cb_ot_gender.addItem("Male", 0); self.cb_ot_gender.addItem("Female", 1)
        of.addRow("OT gender", self.cb_ot_gender)
        rl.addWidget(obox)

        row = QHBoxLayout()
        self.btn_apply = QPushButton("Apply to Pokémon"); self.btn_apply.setObjectName("primary")
        self.btn_apply.clicked.connect(self.apply)
        self.btn_revert = QPushButton("Revert"); self.btn_revert.clicked.connect(self.refresh_fields)
        row.addWidget(self.btn_apply); row.addWidget(self.btn_revert)
        rl.addLayout(row); rl.addStretch(1)
        self._set_enabled(False)
        self.cb_species.currentIndexChanged.connect(self._update_sprite)
        self.chk_shiny.toggled.connect(self._update_sprite)

    def _other_theme_label(self):
        return "Emerald theme" if self.theme_name == "teal" else "Teal theme"

    def toggle_theme(self):
        self.theme_name = "emerald" if self.theme_name == "teal" else "teal"
        theme.apply_theme(QApplication.instance(), self, self.theme_name)
        self.btn_theme.setText(self._other_theme_label())

    def _set_enabled(self, on):
        for w in (self.cb_species, self.le_nick, self.cb_nature, self.cb_ability, self.sp_level,
                  self.sp_friend, self.cb_item, self.cb_gender, self.chk_shiny,
                  self.btn_apply, self.btn_revert,
                  self.sp_pokerus_strain, self.sp_pokerus_days, self.sp_met_level,
                  self.sp_met_location, self.cb_origin_game, self.cb_ball, self.cb_ot_gender,
                  *self.cb_move, *self.sp_pp, *self.sp_ppup, *self.sp_iv, *self.sp_ev,
                  *self.sp_contest.values()):
            w.setEnabled(on)

    def _ev_total(self):
        t = sum(w.value() for w in self.sp_ev)
        self.lbl_evtot.setText(f"EV total: {t} / 510" + ("  ⚠ over cap" if t > 510 else ""))

    # ---------------- load ----------------
    def act_open_save(self):
        p, _ = QFileDialog.getOpenFileName(self, "Open save", "", "Saves (*.sav *.srm);;All files (*)")
        if p: self.load_save(p, self.rom_path)
    def act_open_rom(self):
        p, _ = QFileDialog.getOpenFileName(self, "Open ROM", "", "GBA (*.gba);;All (*)")
        if p and self.save: self.load_save(self.save.path, p)
        elif p: self.rom_path = p

    def _fill_combo(self, combo, pairs):
        combo.blockSignals(True); combo.clear()
        for idx, nm in pairs: combo.addItem(nm, idx)
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
        if rom_path and not self.save.rom_ok:
            QMessageBox.warning(self, "ROM not recognised",
                "This file doesn't look like a supported Pokémon Emerald Seaglass ROM, "
                "so Pokémon / move / item names and sprites may be missing or wrong.\n\n"
                "Make sure you opened the Seaglass .gba itself (not a different game), and "
                "that it matches the version your save was made on. If you're on a newer or "
                "older Seaglass release and this keeps happening, let me know which version "
                "so it can be supported.")

    def status(self):
        if not self.save: return
        bad = self.save.verify_checksums(); t = self.save.trainer()
        if not self.rom_path:
            rom = "no ROM — load it for names/stats"
        elif not self.save.rom_ok:
            rom = "ROM not recognised — names/sprites may be wrong"
        else:
            rom = "ROM loaded"
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
            ic = self._mon_icon(m.species, m.shiny)
            if ic: it.setIcon(ic)
            it.setData(Qt.UserRole, ("party", i)); self.list.addItem(it)
        h2 = QListWidgetItem("— PC BOXES —"); h2.setFlags(Qt.NoItemFlags); self.list.addItem(h2)
        for off, m in self.save.box_mons():
            it = QListWidgetItem(f"  {self.save.species_name(m.species)}  '{m.nickname}'")
            ic = self._mon_icon(m.species, m.shiny)
            if ic: it.setIcon(ic)
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

    def _mon_icon(self, species, shiny):
        if not self.rom_path: return None
        key = (species, bool(shiny))
        if key in self._icon_cache: return self._icon_cache[key]
        res = self.save.sprite_rgba(species, shiny)
        icon = None
        if res:
            W, H, buf = res
            img = QImage(buf, W, H, QImage.Format_RGBA8888)
            icon = QIcon(QPixmap.fromImage(img).scaled(34, 34, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        self._icon_cache[key] = icon
        return icon

    def _update_sprite(self):
        if not self.cur or not self.rom_path:
            self.sprite_label.clear(); return
        cb = self.cb_species
        if cb.currentData() is not None and cb.currentText() == cb.itemText(cb.currentIndex()):
            sp = cb.currentData()
        else:
            sp = self.cur[2].species
        res = self.save.sprite_rgba(sp, self.chk_shiny.isChecked())
        if not res:
            self.sprite_label.clear(); return
        W, H, buf = res
        img = QImage(buf, W, H, QImage.Format_RGBA8888)
        self.sprite_label.setPixmap(
            QPixmap.fromImage(img).scaled(W*2, H*2, Qt.KeepAspectRatio, Qt.FastTransformation))

    def refresh_fields(self):
        if not self.cur: return
        kind, ref, m = self.cur
        self._set_combo(self.cb_species, m.species, f"#{m.species}")
        self.le_nick.setText(m.nickname)
        self.cb_nature.setCurrentIndex(m.nature)
        # ability (per-species options)
        abils = self.save.species_abilities(m.species) if self.rom_path else []
        self.cb_ability.blockSignals(True); self.cb_ability.clear()
        if abils:
            for slot, aid, nm in abils: self.cb_ability.addItem(nm, slot)
            if len(abils) > 1:
                self.cb_ability.setEnabled(True)
                ix = self.cb_ability.findData(m.ability_slot)
                self.cb_ability.setCurrentIndex(ix if ix >= 0 else 0)
            else:
                self.cb_ability.setEnabled(False); self.cb_ability.setCurrentIndex(0)
        else:
            self.cb_ability.addItem("?", 0); self.cb_ability.setEnabled(False)
        self.cb_ability.blockSignals(False)
        # gender
        ratio = self.save.gender_ratio(m.species) if self.rom_path else None
        self.cb_gender.blockSignals(True); self.cb_gender.clear()
        if ratio is None:
            self.cb_gender.addItem("—", None); self.cb_gender.setEnabled(False)
        elif ratio == 255:
            self.cb_gender.addItem("Genderless", "N"); self.cb_gender.setEnabled(False)
        elif ratio == 0:
            self.cb_gender.addItem("Male", "M"); self.cb_gender.setEnabled(False)
        elif ratio == 254:
            self.cb_gender.addItem("Female", "F"); self.cb_gender.setEnabled(False)
        else:
            self.cb_gender.addItem("Male", "M"); self.cb_gender.addItem("Female", "F")
            self.cb_gender.setEnabled(True)
            self.cb_gender.setCurrentIndex(0 if self.save.gender_of(m.pv, m.species) == 'M' else 1)
        self.cb_gender.blockSignals(False)
        # shiny
        self.chk_shiny.blockSignals(True)
        self.chk_shiny.setChecked(m.shiny); self.chk_shiny.setEnabled(bool(self.rom_path))
        self.chk_shiny.blockSignals(False)
        self.sp_level.setValue(m.level or 1)
        self.sp_friend.setValue(m.friendship)
        self._set_combo(self.cb_item, m.held_item, self.save.item_name(m.held_item))
        for i in range(4):
            self._set_combo(self.cb_move[i], m.moves[i], self.save.move_name(m.moves[i]))
            self.sp_pp[i].setValue(m.pp[i])
        ppup = m.pp_up
        for i in range(4): self.sp_ppup[i].setValue(ppup[i])
        ivs, evs = m.ivs, m.evs
        for i, k in enumerate(STAT_KEYS):
            self.sp_iv[i].setValue(ivs[k]); self.sp_ev[i].setValue(evs[k])
        self._ev_total()
        contest = m.contest
        for k, sp in self.sp_contest.items(): sp.setValue(contest[k])
        self.sp_pokerus_strain.setValue(m.pokerus_strain)
        self.sp_pokerus_days.setValue(m.pokerus_days)
        self.sp_met_level.setValue(m.met_level)
        self.sp_met_location.setValue(m.met_location)
        self.cb_origin_game.setCurrentIndex(m.origin_game)
        self.cb_ball.setCurrentIndex(m.poke_ball)
        self.cb_ot_gender.setCurrentIndex(m.ot_gender)
        base = self.save.base_stats(m.species) if self.rom_path else [0]*6
        g = self.save.gender_of(m.pv, m.species) if self.rom_path else "?"
        self.lbl_info.setText(
            f"OT {m.ot_name}   gender {g}   {'SHINY ' if m.shiny else ''}"
            f"ability slot {m.ability_slot}\nbase stats  " +
            "  ".join(f"{l}:{v}" for l, v in zip(STAT_LABELS, base)))
        self._update_sprite()

    # ---------------- apply ----------------
    def _combo_id(self, combo, name2id, fallback):
        d = combo.currentData()
        if d is not None and combo.currentText() == combo.itemText(combo.currentIndex()):
            return d
        return name2id.get(combo.currentText().strip(), fallback)

    def apply(self):
        if not self.cur: return
        kind, ref, m = self.cur
        # species: change ONLY if a real dropdown item is selected — otherwise keep
        # the current one (prevents corrupting species that aren't in the list)
        cb = self.cb_species
        if cb.currentData() is not None and cb.currentText() == cb.itemText(cb.currentIndex()):
            m.species = cb.currentData()
        m.nickname = self.le_nick.text()
        m.friendship = self.sp_friend.value()
        m.held_item = self._combo_id(self.cb_item, self._item_name2id, m.held_item)
        m.moves = [self._combo_id(self.cb_move[i], self._move_name2id, m.moves[i]) for i in range(4)]
        m.pp = [w.value() for w in self.sp_pp]
        m.pp_up = [w.value() for w in self.sp_ppup]
        m.ivs = {k: self.sp_iv[i].value() for i, k in enumerate(STAT_KEYS)}
        m.evs = {k: self.sp_ev[i].value() for i, k in enumerate(STAT_KEYS)}
        m.contest = {k: sp.value() for k, sp in self.sp_contest.items()}
        m.set_pokerus(self.sp_pokerus_strain.value(), self.sp_pokerus_days.value())
        m.met_level = self.sp_met_level.value()
        m.met_location = self.sp_met_location.value()
        m.origin_game = self.cb_origin_game.currentData()
        m.poke_ball = self.cb_ball.currentData()
        m.ot_gender = self.cb_ot_gender.currentData()
        if self.cb_ability.isEnabled() and self.rom_path:
            m.ability_slot = self.cb_ability.currentData()
        # nature / gender / shiny -> one PV reroll preserving the others
        want_nat = self.cb_nature.currentIndex()
        want_shiny = self.chk_shiny.isChecked()
        want_gender = self.cb_gender.currentData() if self.cb_gender.isEnabled() else None
        cur_gender = self.save.gender_of(m.pv, m.species) if self.rom_path else None
        gender_changed = (want_gender is not None and want_gender != cur_gender)
        if want_nat != m.nature or want_shiny != m.shiny or gender_changed:
            if self.rom_path:
                m.pv = self.save.reroll_pv(m, nature=want_nat, shiny=want_shiny, gender=want_gender)
            else:
                QMessageBox.information(self, "Needs ROM",
                    "Load the ROM to change nature, gender, or shininess safely.")
        if kind == "party":
            self.save.set_level(m, self.sp_level.value())
            if self.rom_path: self.save.recompute_stats(m)
            self.save.write_party_mon(ref, m)
        else:
            self.save.write_box_mon(ref, m)
        bad = self.save.verify_checksums()
        self.populate_list(); self.status(); self.refresh_fields()
        QMessageBox.information(self, "Applied",
            f"Changes written to the in-memory save.\nChecksums: {'OK' if not bad else 'BAD '+str(bad)}\n\n"
            "Use “Save As…” to write the .sav file.")

    def act_save_as(self):
        if not self.save: return
        base = os.path.splitext(os.path.basename(self.save.path))[0]
        ext = ".srm" if self.save.path.lower().endswith(".srm") else ".sav"
        p, _ = QFileDialog.getSaveFileName(self, "Save As", base + "_edited" + ext, "Saves (*.sav *.srm)")
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
