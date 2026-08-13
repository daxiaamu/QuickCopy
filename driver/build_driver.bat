@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set "KITROOT=C:\Program Files (x86)\Windows Kits\10"
set "KITVER=10.0.26100.0"
set "OUT=x64\Release"

if not exist "%VSWHERE%" (
  echo [ERROR] Visual Studio Installer vswhere.exe was not found.
  exit /b 1
)
for /f "tokens=*" %%i in ('vswhere.exe -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VSROOT=%%i"
if not defined VSROOT (
  echo [ERROR] Visual Studio 2022 C++ x64 tools were not found.
  exit /b 1
)
if not exist "%KITROOT%\Include\%KITVER%\km\ntifs.h" (
  echo [ERROR] WDK %KITVER% kernel headers were not found.
  exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

cl /nologo /c /kernel /W4 /WX /O2 /GS- /GR- /EHs-c- /Zl ^
  /D_AMD64_ /DAMD64 /D_WIN64 /DNTDDI_VERSION=0x0A00000C /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
  /I"%KITROOT%\Include\%KITVER%\km" ^
  /I"%KITROOT%\Include\%KITVER%\km\crt" ^
  /I"%KITROOT%\Include\%KITVER%\shared" ^
  quickcopy_kbd.c /Fo:"%OUT%\quickcopy_kbd.obj"
if errorlevel 1 exit /b 1

link /nologo /OUT:"%OUT%\QuickCopyKbd.sys" /SUBSYSTEM:NATIVE,10.00 /DRIVER /ENTRY:DriverEntry ^
  /NODEFAULTLIB /INCREMENTAL:NO /OPT:REF /OPT:ICF /MACHINE:X64 /MANIFEST:NO ^
  "%OUT%\quickcopy_kbd.obj" /LIBPATH:"%KITROOT%\Lib\%KITVER%\km\x64" ^
  ntoskrnl.lib hal.lib wdmsec.lib BufferOverflowK.lib
if errorlevel 1 exit /b 1

copy /y QuickCopyKbd.inf "%OUT%\QuickCopyKbd.inf" >nul
"%KITROOT%\bin\%KITVER%\x86\Inf2Cat.exe" /driver:"%OUT%" /os:10_X64
if errorlevel 1 exit /b 1

echo [OK] %OUT%\QuickCopyKbd.sys and QuickCopyKbd.cat built.