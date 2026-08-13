@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo Compiling QuickCopy...
windres -O coff -o app.o app.rc
if %errorlevel% neq 0 (
    echo [FAILED] resource compile failed
    pause
    exit /b 1
)
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -mwindows -o QuickCopy.exe quick_copy.c json_helper.c app.o -luser32 -lkernel32 -lgdi32 -lcomctl32 -lshell32 -ladvapi32 -lsetupapi
if %errorlevel% equ 0 (
    echo [OK] QuickCopy.exe built
) else (
    echo [FAILED]
)
pause
