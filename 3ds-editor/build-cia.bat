@echo off
REM Build the CIA. Run this AFTER "make" has produced SeaglassSaveEditor.elf.
REM Needs bannertool.exe and makerom.exe next to this file (or on PATH).
REM   bannertool: github.com/Steveice10/bannertool/releases
REM   makerom:    github.com/3DSGuy/Project_CTR/releases

bannertool makebanner -i meta\banner.png -a meta\audio.wav -o meta\banner.bnr
if errorlevel 1 goto fail
bannertool makesmdh -s "Seaglass Save Editor" -l "Edit Emerald Seaglass saves on console" -p "kinge" -i meta\icon.png -o meta\icon.icn
if errorlevel 1 goto fail
makerom -f cia -o SeaglassSaveEditor.cia -elf SeaglassSaveEditor.elf -rsf meta\app.rsf -icon meta\icon.icn -banner meta\banner.bnr -exefslogo -target t
if errorlevel 1 goto fail
echo.
echo Done! Install SeaglassSaveEditor.cia with FBI.
goto end
:fail
echo BUILD FAILED - check the error above.
:end
pause
