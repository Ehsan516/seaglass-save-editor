@echo off
REM Build the CIA. Run this AFTER "make" has produced SeaglassSaveEditor.elf.
REM Needs bannertool.exe and makerom.exe next to this file (or on PATH).
REM   bannertool: github.com/Steveice10/bannertool/releases
REM   makerom:    github.com/3DSGuy/Project_CTR/releases

bannertool makebanner -ci meta\banner.cgfx -a meta\audio.wav -o meta\banner.bnr
if %errorlevel% neq 0 goto fail
bannertool makesmdh -s "Seaglass Save Editor" -l "Edit Emerald Seaglass saves on console" -p "ehsan516" -i meta\icon.png -o meta\icon.icn
if %errorlevel% neq 0 goto fail
makerom -f cia -o SeaglassSaveEditor.cia -elf SeaglassSaveEditor.elf -rsf meta\app.rsf -icon meta\icon.icn -banner meta\banner.bnr -exefslogo -target t
if %errorlevel% neq 0 goto fail
echo.
echo Done! Install SeaglassSaveEditor.cia with FBI.
goto end
:fail
echo BUILD FAILED - check the error above.
:end
pause
