@echo off
REM ============================================================
REM  ECG GUI - Build Windows executable with PyInstaller
REM ============================================================
cd /d "%~dp0"
setlocal

echo.
echo === ECG GUI Build Script ===
echo.

REM ---- Check Python ----
set PYTHON=
py -3 -c "import sys" >nul 2>nul
if %errorlevel% equ 0 (
    set PYTHON=py -3
) else (
    where python >nul 2>nul
    if %errorlevel% equ 0 (
        set PYTHON=python
    )
)

if "%PYTHON%" == "" (
    echo [ERROR] Python not found in PATH.
    echo          Install from https://www.python.org/downloads/ or use the py launcher.
    pause
    exit /b 1
)

echo [1/3] Checking dependencies...

REM Only touch the network when a dependency is actually missing.
%PYTHON% -c "import serial" >nul 2>nul
if %errorlevel% neq 0 (
    echo      pyserial missing - installing...
    %PYTHON% -m pip install pyserial
    if %errorlevel% neq 0 goto :depfail
)
%PYTHON% -c "import matplotlib" >nul 2>nul
if %errorlevel% neq 0 (
    echo      matplotlib missing - installing...
    %PYTHON% -m pip install matplotlib
    if %errorlevel% neq 0 goto :depfail
)
%PYTHON% -c "import PyInstaller" >nul 2>nul
if %errorlevel% neq 0 (
    echo      pyinstaller missing - installing...
    %PYTHON% -m pip install pyinstaller
    if %errorlevel% neq 0 goto :depfail
)

echo      Dependencies OK.
echo [2/3] Building executable with PyInstaller...
if not exist dist mkdir dist
%PYTHON% -m PyInstaller --name ECG_GUI --onefile --windowed --add-data "ecg_gui.py;." ecg_gui.py
if %errorlevel% neq 0 goto :buildfail

echo [3/3] Copying to dist folder...
if exist dist\ECG_GUI.exe (
    echo.
    echo === Build completed successfully ===
    echo Executable: %~dp0dist\ECG_GUI.exe
) else (
    echo [WARN] ECG_GUI.exe not found in dist/
)

pause
exit /b 0

:depfail
echo [ERROR] Failed to install dependencies.
pause
exit /b 1

:buildfail
echo [ERROR] PyInstaller build failed.
pause
exit /b 1
