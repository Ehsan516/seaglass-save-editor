@echo off
REM ── Build SeaglassSaveEditor.exe (end users won't need Python or an IDE) ──
echo Installing build deps...
python -m pip install --upgrade pyinstaller PySide6 >nul 2>&1
echo Building...
python -m PyInstaller --onefile --windowed --name SeaglassSaveEditor --icon app.ico seaglass_editor.py
echo.
echo Done.  Your standalone app is here:  dist\SeaglassSaveEditor.exe
pause
