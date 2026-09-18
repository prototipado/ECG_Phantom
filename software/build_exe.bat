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
where python >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] Python not found in PATH.
    echo          Install from https://www.python.org/downloads/
    pause
    exit /b 1
)

echo [1/3] Checking dependencies...

REM Only touch the network when a dependency is actually missing. An eager
REM "pip install --upgrade ..." round-trips to PyPI even when everything is
REM already installed and can hang indefinitely on a slow/no connection.
python -c "import serial" >nul 2>nul
if %errorlevel% neq 0 (
    echo      pyserial missing - installing...
    python -m pip install pyserial
    if %errorlevel% neq 0 goto :depfail
)
python -c "import matplotlib" >nul 2>nul
if %errorlevel% neq 0 (
    echo      matplotlib missing - installing...
    python -m pip install matplotlib
    if %errorlevel% neq 0 goto :depfail
)
python -c "import PyInstaller" >nul 2>nul
if %errorlevel% neq 0 (
    echo      pyinstaller missing - installing...
    python -m pip install pyinstaller
    if %errorlevel% neq 0 goto :depfail
)
echo      All dependencies present.
echo.
goto :build

:depfail
echo [ERROR] Failed to install a dependency.
pause
exit /b 1

:build
echo [2/3] Building executable (one-file, no console window)...
REM The GUI now performs an explicit serial-thread shutdown before Tk exits.
REM Rebuild the executable after changing ecg_gui.py so the packaged copy
REM contains the same cleanup path as the source version.
python -m PyInstaller --noconfirm --onefile --windowed ^
    --name "ECG_GUI" ^
    --icon NONE ^
    ecg_gui.py
if %errorlevel% neq 0 (
    echo [ERROR] PyInstaller build failed.
    pause
    exit /b 1
)

echo.
echo [3/3] Build complete!
echo.
echo Executable created at:  %~dp0dist\ECG_GUI.exe
echo.
echo You can copy ECG_GUI.exe anywhere; it does not need Python.
echo.

pause
endlocal