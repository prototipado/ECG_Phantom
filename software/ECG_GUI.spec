# -*- mode: python ; coding: utf-8 -*-

a = Analysis(
    ['ecg_gui.py'],
    pathex=[],
    binaries=[],
    # Bundle the icon image and script so they are available at runtime inside the .exe
    datas=[
        ('ecg_gui.py', '.'),
        ('images/icono.png', 'images'),
        ('images/icono.ico', 'images'),
    ],
    hiddenimports=[
        'PIL',
        'matplotlib.backends.backend_tkagg',
    ],
    hookspath=[],
    hooksconfig={
        'matplotlib': {
            'backends': ['TkAgg']
        }
    },
    runtime_hooks=[],
    excludes=[
        # PyQt6 / Qt — GUI uses tkinter; matplotlib Qt backend not needed
        'PyQt6', 'PyQt5', 'PySide2', 'PySide6',
        'PyQt6.QtCore', 'PyQt6.QtGui', 'PyQt6.QtWidgets',
        'matplotlib.backends.backend_qt5agg',
        'matplotlib.backends.backend_qt6agg',
        'matplotlib.backends.backend_qtagg',
    ],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='ECG_GUI',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,   # UPX disabled to prevent native .pyd/.dll corruption
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon='images/icono.ico',
)
