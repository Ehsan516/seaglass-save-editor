from PySide6.QtWidgets import QGroupBox, QGraphicsDropShadowEffect, QPushButton, QLabel
from PySide6.QtGui import QColor, QFont

# ---- palette ----
SHELL0   = "#16352c"; SHELL1   = "#0d201a"   # device shell gradient
BEZEL    = "#0a1812"                          # toolbar / status bezel
PLATE0   = "#f6faf7"; PLATE1   = "#d9e8df"    # raised plate (light frosted)
PLATE_BD = "#9fbcae"
ENGRAVE  = "#1d3a30"                          # dark text on plates
FIELD0   = "#c6dacd"; FIELD1   = "#eef5f0"    # inset field (dark top -> light)
FIELD_BD = "#7f9e8f"
ACCENT0  = "#46c98a"; ACCENT1  = "#1f7d4f"    # emerald button
ACCENT_BD= "#15623c"
LCD0     = "#bcdac8"; LCD1     = "#9ec6ad"    # LCD header

QSS = f"""
* {{
    font-family: "Segoe UI", "DejaVu Sans", "Helvetica Neue", sans-serif;
    font-size: 12px;
}}
QMainWindow, QWidget#root {{
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 {SHELL0}, stop:1 {SHELL1});
}}
QLabel {{ color: #dff0e7; background: transparent; }}
QGroupBox QLabel {{ color: {ENGRAVE}; }}

/* ---- LCD header ---- */
QLabel#header {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 {LCD0}, stop:1 {LCD1});
    color: #123026;
    border: 2px solid #5f7e6f;
    border-radius: 12px;
    padding: 12px 16px;
    font-family: "Consolas", "DejaVu Sans Mono", monospace;
    font-size: 16px;
    font-weight: 800;
}}

/* ---- inset LCD readout ---- */
QLabel#info {{
    background: rgba(0,0,0,0.20);
    color: #cfe9da;
    border: 1px solid rgba(255,255,255,0.08);
    border-radius: 8px;
    padding: 8px 10px;
    font-family: "Consolas", "DejaVu Sans Mono", monospace;
}}

/* ---- raised frosted plates ---- */
QGroupBox {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 {PLATE0}, stop:1 {PLATE1});
    border: 1px solid {PLATE_BD};
    border-radius: 14px;
    margin-top: 16px;
    padding: 14px 12px 12px 12px;
    font-weight: 700;
    color: {ENGRAVE};
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 16px;
    padding: 3px 12px;
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 {ACCENT0}, stop:1 {ACCENT1});
    color: #ffffff;
    border: 1px solid {ACCENT_BD};
    border-radius: 8px;
}}

/* ---- tactile buttons ---- */
QPushButton {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #fbfdfc, stop:0.5 #e6f0ea, stop:1 #cddccf);
    border: 1px solid {PLATE_BD};
    border-bottom: 3px solid #7d9b8c;
    border-radius: 10px;
    padding: 7px 16px;
    color: {ENGRAVE};
    font-weight: 700;
}}
QPushButton:hover {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #ffffff, stop:0.5 #eef6f0, stop:1 #d8e6da);
}}
QPushButton:pressed {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #cddccf, stop:1 #eef6f0);
    border-bottom: 1px solid {PLATE_BD};
    border-top: 3px solid #7d9b8c;
    padding-top: 9px;
}}
QPushButton#primary {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 {ACCENT0}, stop:1 {ACCENT1});
    color: #ffffff;
    border: 1px solid {ACCENT_BD};
    border-bottom: 3px solid #0f4d2f;
}}
QPushButton#primary:hover {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #5fd89b, stop:1 #239059);
}}
QPushButton#primary:pressed {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 {ACCENT1}, stop:1 {ACCENT0});
    border-bottom: 1px solid {ACCENT_BD};
    border-top: 3px solid #0f4d2f;
    padding-top: 9px;
}}

/* ---- grooved (recessed) inputs ---- */
QComboBox, QLineEdit, QSpinBox {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 {FIELD0}, stop:0.10 #d8e8df, stop:1 {FIELD1});
    border: 1px solid {FIELD_BD};
    border-top: 2px solid #6b8a7b;
    border-radius: 8px;
    padding: 5px 10px;
    min-height: 22px;
    color: #16302a;
    selection-background-color: {ACCENT1};
    selection-color: #ffffff;
}}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus {{ border: 1px solid {ACCENT0}; border-top: 2px solid {ACCENT1}; }}
QComboBox::drop-down {{ border: 0; width: 26px; }}
QComboBox::down-arrow {{
    width: 0; height: 0;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 7px solid #2f5a48;
    margin-right: 9px;
}}
QComboBox QAbstractItemView {{
    background: #eef5f0;
    border: 1px solid {FIELD_BD};
    selection-background-color: {ACCENT1};
    selection-color: #ffffff;
    outline: 0;
}}
QSpinBox::up-button, QSpinBox::down-button {{
    width: 16px; background: #d2e3d8; border-left: 1px solid {FIELD_BD};
}}
QSpinBox::up-button {{ border-top-right-radius: 7px; }}
QSpinBox::down-button {{ border-bottom-right-radius: 7px; }}

/* ---- inset LCD screen (the list) ---- */
QListWidget {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #bcd4c5, stop:1 #d4e5da);
    border: 2px solid #6b8a7b;
    border-radius: 12px;
    padding: 6px;
    color: #16302a;
    outline: 0;
}}
QListWidget::item {{ padding: 8px 8px; border-radius: 7px; margin: 1px; }}
QListWidget::item:selected {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 {ACCENT0}, stop:1 {ACCENT1});
    color: #ffffff;
}}
QListWidget::item:hover:!selected {{ background: rgba(31,125,79,0.16); }}

/* ---- device bezels ---- */
QToolBar {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 {SHELL0}, stop:1 {BEZEL});
    border: 0; padding: 8px; spacing: 8px;
}}
QToolBar QToolButton {{
    color: #e6f4ec;
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.14);
    border-radius: 9px;
    padding: 7px 14px;
    font-weight: 700;
}}
QToolBar QToolButton:hover {{ background: rgba(95,216,155,0.22); }}
QToolBar QToolButton:pressed {{ background: rgba(31,125,79,0.40); }}
QStatusBar {{ background: {BEZEL}; color: #bfe0cf; }}
QStatusBar::item {{ border: 0; }}
QSplitter::handle {{ background: transparent; }}
QScrollArea#editorScroll {{ background: transparent; border: 0; }}
QScrollArea#editorScroll > QWidget > QWidget {{ background: transparent; }}
QScrollBar:vertical {{ background: transparent; width: 12px; margin: 2px; }}
QScrollBar::handle:vertical {{ background: #5f8170; border-radius: 5px; min-height: 24px; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; }}
"""

def _shadow(widget, blur=22, dy=4, alpha=110):
    eff = QGraphicsDropShadowEffect(widget)
    eff.setBlurRadius(blur); eff.setXOffset(0); eff.setYOffset(dy)
    eff.setColor(QColor(0, 0, 0, alpha))
    widget.setGraphicsEffect(eff)

def apply_theme(app, window):
    app.setStyle("Fusion")
    app.setStyleSheet(QSS)
    # soft drop shadows on raised elements for tactile depth
    for gb in window.findChildren(QGroupBox):
        _shadow(gb, blur=24, dy=4, alpha=90)
    for btn in window.findChildren(QPushButton):
        _shadow(btn, blur=12, dy=2, alpha=70)
    hdr = window.findChild(QLabel, "header")
    if hdr:
        _shadow(hdr, blur=18, dy=3, alpha=120)
        f = hdr.font(); f.setLetterSpacing(QFont.AbsoluteSpacing, 2.0); hdr.setFont(f)
