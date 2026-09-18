#!/usr/bin/env python3
"""
ECG Streaming GUI - Tkinter + matplotlib
Connects to Pico ECG firmware via USB serial, sends CLI commands,
receives real-time ECG data, and displays waveforms with parameter controls.

Usage:
    python ecg_gui.py

Requirements:
    pip install pyserial matplotlib
"""

import sys
import time
import struct
import threading
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from collections import deque
from itertools import islice
import queue
import csv
import datetime
import os

try:
    import numpy as np
except ImportError:
    np = None

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

try:
    import matplotlib
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
    from matplotlib.figure import Figure
except ImportError:
    print("ERROR: matplotlib not installed. Run: pip install matplotlib")
    sys.exit(1)


# =============================================================================
# Project Configuration
# =============================================================================

# Key used internally to identify the pico_ecg firmware (stored-signal player).
# It is NOT in PROJECTS because it has no models/params in the traditional sense.
PICO_ECG_KEY = "pico_ecg"
# Key used internally to identify the pace_sim firmware (pacemaker test bench).
PACE_SIM_KEY = "pace_sim"
# Key used internally to identify the pace_sim_intracardiac firmware.
PACE_SIM_IC_KEY = "pace_sim_intracardiac"
PACE_SIM_9_KEY = "pace_sim_intracardiac_9_nodes"
# Indices in the 7-channel streamed frames holding the pace-sense ADC
# voltages (atrial, ventricular, surface). Both bench firmwares stream 7
# channels with the ADC slots last, though the other 4 channels differ:
# pace_sim sends 0-2 ECG (RA/LA/LL) + 3 PPG; pace_sim_intracardiac sends
# 0-3 cell-model EGMs (RA, LA, RV, LV). See each streaming.h.
PACE_ADC_CHANNELS = (4, 5, 6)

# Pace-sense detection configuration shown as live sliders in the dedicated
# "Detección Pace-sense" card (pace_sim only). Each entry is:
#   (CLI param, label, unit, min, max, default, resolution)
# Sent to the firmware as '> set <param> <value>' on slider release; defaults
# match pace_sense_init() / the firmware so the GUI state agrees with boot.
PACE_DETECT_PARAMS = [
    ('athresh',    'Atrial Thresh',      'V',     0.0,  3.5,  2.7,  0.01),
    ('vthresh',    'Ventric. Thresh',    'V',     0.0,  3.5,  2.7,  0.01),
    ('acrossback', 'Atrial Crossback',   'V',     0.0,  3.5,  2.3,  0.01),
    ('vcrossback', 'Ventric. Crossback', 'V',     0.0,  3.5,  2.3,  0.01),
    ('adebounce',  'Atrial Debounce',    'smp',   1,    64,   3,    1),
    ('vdebounce',  'Ventric. Debounce',  'smp',   1,    64,   3,    1),
    ('arefrac',    'Atrial Refrac.',     'ms',    0,    500,  150,  1),
    ('vrefrac',    'Ventric. Refrac.',   'ms',    0,    500,  150,  1),
    ('icgain',     'IC Gain',            'x',     0.05, 10.0, 1.0,  0.01),
]

# Hover-help text for each pace-sense detection parameter (shown as a tooltip).
PACE_DETECT_HELP = {
    'athresh':    ('Umbral alto (V) del detector atrial. La detección es por '
                   'flanco ASCENDENTE: la señal sensada reposa en ~1.9 V y un '
                   'pulso del DUT sube a ~3.2 V. Si lo subes, ignoras pulsos '
                   'débiles; si lo bajas, aumentas falsos positivos por ruido.'),
    'vthresh':    ('Umbral alto (V) del detector ventricular. Mismo '
                   'comportamiento que athresh pero sobre el canal V.'),
    'acrossback': ('Umbral de rearme (V) del canal atrial: después de detectar '
                   'un pulso, la señal debe CAER por debajo de este nivel para '
                   'volver a armar el detector. Valor típico ~2.3 V (entre el '
                   'idle y el pico). Si lo acercas al pico, el detector tarda '
                   'más en rearmarse.'),
    'vcrossback': ('Umbral de rearme (V) del canal ventricular. Igual que '
                   'acrossback pero sobre V.'),
    'adebounce':  ('Muestras consecutivas sobre el umbral que deben pasar para '
                   'confirmar un latido atrial (anti-ruido). Más alto = más '
                   'inmune al ruido pero añade retardo y puede perder pulsos '
                   'cortos. ~3 muestras = ~0.6 ms a 5 kHz.'),
    'vdebounce':  ('Muestras consecutivas sobre el umbral para confirmar un '
                   'latido ventricular. Igual que adebounce pero sobre V.'),
    'arefrac':    ('Período refractario atrial (ms): tras un latido, el '
                   'detector se ignora durante este tiempo para no contar dos '
                   'veces el mismo pulso (p.ej. cola del artefacto).'),
    'vrefrac':    ('Período refractario ventricular (ms). Igual que arefrac '
                   'pero sobre V.'),
    'icgain':     ('Ganancia del amplificador de instrumentación (x) aplicada '
                   'a las señales. Ajusta la sensibilidad frente al ruido del '
                   'front-end; satura/desborda la señal si lo subes demasiado.'),
}

PROJECTS = {
    "pico_ecg_conduction_model": {
        "name": "Conduction Model",
        "paper": "Quiroz-Juarez 2018",
        "desc": "VdP + FitzHugh-Nagumo",
        "leads": ["RA", "LA(I)", "LL(II)", "V1", "V2", "V3", "V4", "V5", "V6"],
        "num_leads": 9,
        "params": {
            "hr":        {"label": "Heart Rate",    "unit": "bpm",  "min": 15,   "max": 250,   "default": 72,   "resolution": 1, "help": "Frecuencia cardíaca del modelo en latidos/minuto (bpm). Cambia la cadencia del ECG."},
            "amp":       {"label": "Amplitude",     "unit": "%",    "min": 0,    "max": 100,   "default": 100,  "resolution": 1, "help": "Escala (%) del ECG respecto al rango de salida. Bájalo para señales menos intensas o para evitar saturar el front-end."},
            "a1":        {"label": "Alpha1 (P)",    "unit": "",     "min": 0,    "max": 2,     "default": 0.5,  "resolution": 0.01, "help": "Peso de la onda P (primera gaussiana). Más alto = onda P más prominente."},
            "a2":        {"label": "Alpha2 (Ta)",   "unit": "",     "min": -2,   "max": 0,     "default": -0.2, "resolution": 0.01, "help": "Peso de la onda Ta (onda T atrial, negativa). Ajusta la muesca/repolarización atrial."},
            "a3":        {"label": "Alpha3 (QRS)",  "unit": "",     "min": 0,    "max": 5,     "default": 1.5,  "resolution": 0.01, "help": "Peso del complejo QRS (pico principal). Impacta la amplitud del latido ventricular."},
            "a4":        {"label": "Alpha4 (T)",    "unit": "",     "min": 0,    "max": 2,     "default": 0.3,  "resolution": 0.01, "help": "Peso de la onda T (repolarización ventricular). Ajusta su prominencia."},
            "delay":     {"label": "Node Delay",    "unit": "ms",   "min": 0,    "max": 200,   "default": 35,   "resolution": 1, "help": "Retardo del nodo AV (ms). Simula el retardo A-V: más largo = PR más largo."},
            "baseamp":   {"label": "Baseline Amp",  "unit": "mV",   "min": 0,    "max": 1,     "default": 0.15, "resolution": 0.01, "help": "Amplitud de la deriva/ruido de línea base (mV). Añade variación lenta a la señal."},
            "basefreq":  {"label": "Baseline Freq", "unit": "Hz",   "min": 0.1,  "max": 1,     "default": 0.35, "resolution": 0.01, "help": "Frecuencia (Hz) de la deriva de línea base. Controla lo rápido que onda la base."},
        },
        "rhythms": [
            "Normal Sinus", "Sinus Bradycardia", "Sinus Tachycardia",
            "First Degree AVB", "Second Degree AVB Type I", "Second Degree AVB Type II",
            "Third Degree AVB", "Ventricular Tachycardia"
        ],
    },
    "pico_ecg_gaussian_ppg": {
        "name": "Gaussian ECG+PPG",
        "paper": "Huynh/Tran 2026",
        "desc": "Gaussian ECG + 5-Gaussian PPG",
        "leads": ["RA", "LA(I)", "LL(II)", "V1", "V2", "V3", "V4", "V5", "V6"],
        "num_leads": 9,
        "params": {
            "hr":        {"label": "Heart Rate",      "unit": "bpm",  "min": 30,   "max": 200,   "default": 72,   "resolution": 1, "help": "Frecuencia cardíaca en latidos/minuto (bpm). Cambia la cadencia del ECG/PPG."},
            "amp":       {"label": "Amplitude",       "unit": "%",    "min": 0,    "max": 100,   "default": 100,  "resolution": 1, "help": "Escala (%) del ECG/PPG respecto al rango de salida."},
            "pr":        {"label": "PR Interval",     "unit": "ms",   "min": 100,  "max": 400,   "default": 180,  "resolution": 1, "help": "Intervalo PR (ms): retardo entre el inicio de la onda P y el QRS. Prolongarlo simula bloqueos AV de 1er grado."},
            "ramp":      {"label": "R Amplitude",     "unit": "",     "min": 0.5,  "max": 2.0,   "default": 1.0,  "resolution": 0.01, "help": "Amplitud de la onda R (normalizada). Más alto = complejo QRS más alto."},
            "tamp":      {"label": "T Amplitude",     "unit": "",     "min": 0.1,  "max": 1.0,   "default": 0.35, "resolution": 0.01, "help": "Amplitud de la onda T (repolarización ventricular)."},
            "pamp":      {"label": "P Amplitude",     "unit": "",     "min": 0.05, "max": 0.5,   "default": 0.15, "resolution": 0.01, "help": "Amplitud de la onda P (despolarización atrial)."},
            "respdepth": {"label": "RSA Depth",       "unit": "",     "min": 0,    "max": 0.3,   "default": 0.05, "resolution": 0.01, "help": "Profundidad de la arritmia sinusal respiratoria (RSA): modulación de la FC con la respiración. 0 = sin modulación."},
            "respfreq":  {"label": "RSA Freq",        "unit": "Hz",   "min": 0.1,  "max": 0.5,   "default": 0.25, "resolution": 0.01, "help": "Frecuencia respiratoria (Hz) que modula la FC. Controla la velocidad del ciclo RSA."},
            "qtexp":     {"label": "QT Exponent",     "unit": "",     "min": 0.3,  "max": 0.7,   "default": 0.5,  "resolution": 0.01, "help": "Exponente de la corrección del intervalo QT con la FC (tipo Bazett/Fridericia). Ajusta cómo cambia el QT."},
        },
        "rhythms": [
            "Normal Sinus", "First-Degree AV Block", "Mobitz I (Wenckebach)",
            "Complete AV Block", "Atrial Tachycardia", "Ventricular Tachycardia"
        ],
    },
    "pace_sim": {
        "name": "Pace Sim (Bench)",
        "paper": "EN 45502-2-1",
        "desc": "Gaussian ECG+PPG + pace-sense ADC",
        "leads": ["RA", "LA(I)", "LL(II)"],
        "num_leads": 3,
        "adc_channels": (4, 5, 6),
        "mv_max": 60,
        "params": {
            "hr":        {"label": "Heart Rate",      "unit": "bpm",    "min": 30,   "max": 200,   "default": 72,    "resolution": 1, "help": "Frecuencia cardíaca en latidos/minuto (bpm) del ritmo intrínseco del bench. En modo ásistole lo ignora."},
            "amp":       {"label": "Amplitude",       "unit": "%",      "min": 0,    "max": 100,   "default": 100,   "resolution": 1, "help": "Escala (%) del ECG/PPG respecto al rango de salida."},
            "pr":        {"label": "PR Interval",     "unit": "ms",     "min": 100,  "max": 400,   "default": 180,   "resolution": 1, "help": "Intervalo PR (ms): retardo entre la onda P y el QRS intrínseco."},
            "ramp":      {"label": "R Amplitude",     "unit": "",       "min": 0.5,  "max": 2.0,   "default": 1.0,   "resolution": 0.01, "help": "Amplitud de la onda R del ritmo intrínseco."},
            "tamp":      {"label": "T Amplitude",     "unit": "",       "min": 0.1,  "max": 1.0,   "default": 0.35,  "resolution": 0.01, "help": "Amplitud de la onda T intrínseca."},
            "pamp":      {"label": "P Amplitude",     "unit": "",       "min": 0.05, "max": 0.5,   "default": 0.15,  "resolution": 0.01, "help": "Amplitud de la onda P intrínseca."},
            "respdepth": {"label": "RSA Depth",       "unit": "",       "min": 0,    "max": 0.3,   "default": 0.05,  "resolution": 0.01, "help": "Profundidad de la modulación respiratoria de la FC (RSA)."},
            "respfreq":  {"label": "RSA Freq",        "unit": "Hz",     "min": 0.1,  "max": 0.5,   "default": 0.25,  "resolution": 0.01, "help": "Frecuencia respiratoria (Hz) que modula la FC intrínseca."},
            "qtexp":     {"label": "QT Exponent",     "unit": "",       "min": 0.3,  "max": 0.7,   "default": 0.5,   "resolution": 0.01, "help": "Exponente de corrección del intervalo QT con la FC."},
        },
        "rhythms": [
            "Normal Sinus", "First-Degree AV Block", "Mobitz I (Wenckebach)",
            "Complete AV Block", "Atrial Tachycardia", "Ventricular Tachycardia"
        ],
    },
    "pace_sim_intracardiac": {
        "name": "Pace Sim IntraCardiac",
        "paper": "Andalam 2017 cell EGM",
        "desc": "4-cell intracardiac EGM + pace-sense ADC",
        "leads": ["RA", "LA", "RV", "LV"],
        "num_leads": 4,
        "adc_channels": (4, 5, 6),
        "mv_max": 150,
        "params": {
            "hr":        {"label": "Heart Rate",      "unit": "bpm",    "min": 30,   "max": 200,   "default": 72,    "resolution": 1, "help": "Frecuencia cardíaca en latidos/minuto (bpm) del ritmo intrínseco. En modo asístole lo ignora."},
            "pr":        {"label": "PR Interval",     "unit": "ms",     "min": 60,   "max": 400,   "default": 160,   "resolution": 1, "help": "Intervalo PR (ms): retardo de conducción A-V. Prolongarlo simula bloqueo AV de 1er grado."},
            "escape":    {"label": "Escape Rate",     "unit": "bpm",    "min": 15,   "max": 120,   "default": 38,    "resolution": 1, "help": "Ritmo de escape ventricular intrínseco (bloqueo AV completo / nodo SA lento)."},
            "amp":       {"label": "Amplitude",       "unit": "%",      "min": 0,    "max": 100,   "default": 100,   "resolution": 1, "help": "Escala (%) de las salidas PWM intracavitarias (EGM). 0-100% escala el duty linealmente."},
            "iadelay":   {"label": "Inter-atrial d.", "unit": "ms",     "min": 0,    "max": 100,   "default": 25,    "resolution": 1, "help": "Retardo interauricular (LA vs RA) del modelo de célula: desfasa el electrograma auricular para simular conducción por haz de Bachmann."},
            "ivdelay":   {"label": "Inter-vent. d.",  "unit": "ms",     "min": 0,    "max": 100,   "default": 15,    "resolution": 1, "help": "Retardo interventricular (LV vs RV) del modelo de célula: simula dispersión normal de conducción."},
            "raerp":     {"label": "RA ERP Scale",    "unit": "x",      "min": 0.5,  "max": 3.0,   "default": 1.6,   "resolution": 0.1, "help": "Pendiente de repolarización/ERP auricular RA (>1 = PA más corto). Cambia la duración del potencial de acción."},
            "laerp":     {"label": "LA ERP Scale",    "unit": "x",      "min": 0.5,  "max": 3.0,   "default": 1.6,   "resolution": 0.1, "help": "Pendiente de repolarización/ERP auricular LA."},
            "rverp":     {"label": "RV ERP Scale",    "unit": "x",      "min": 0.5,  "max": 3.0,   "default": 1.0,   "resolution": 0.1, "help": "Pendiente de repolarización/ERP ventricular RV."},
            "lverp":     {"label": "LV ERP Scale",    "unit": "x",      "min": 0.5,  "max": 3.0,   "default": 1.0,   "resolution": 0.1, "help": "Pendiente de repolarización/ERP ventricular LV."},
        },
        "rhythms": [
            "Normal Sinus", "SA Node Failure"
        ],
    },
    "pace_sim_intracardiac_9_nodes": {
        "name": "Pace Sim Intracardiac 9 Nodes",
        "paper": "Yip 2016 / Andalam 2017 reduced conduction network",
        "desc": "SA-RA/LA-AV-His-RBB/LBB-RV/LV + pace-sense ADC",
        "leads": ["RA", "LA", "RV", "LV"],
        "num_leads": 4,
        "adc_channels": (4, 5, 6),
        "mv_max": 150,
        "params": {
            "hr": {"label": "SA Rate", "unit": "bpm", "min": 15, "max": 200, "default": 72, "resolution": 1, "help": "Frecuencia intrínseca del nodo SA."},
            "amp": {"label": "Amplitude", "unit": "%", "min": 0, "max": 100, "default": 100, "resolution": 1, "help": "Escala de las salidas EGM."},
            "pr": {"label": "AV Delay", "unit": "ms", "min": 20, "max": 400, "default": 120, "resolution": 1, "help": "Retardo AV- His; valores altos simulan primer grado."},
            "path2": {"label": "RA -> AV", "unit": "on", "min": 0, "max": 1, "default": 1, "resolution": 1, "help": "Bloquea la conducción aurícula derecha-nodo AV."},
            "path4": {"label": "AV -> His", "unit": "on", "min": 0, "max": 1, "default": 1, "resolution": 1, "help": "Bloqueo AV completo si se desactiva."},
            "path5": {"label": "His -> RBB", "unit": "on", "min": 0, "max": 1, "default": 1, "resolution": 1, "help": "Bloqueo de rama derecha si se desactiva."},
            "path6": {"label": "His -> LBB", "unit": "on", "min": 0, "max": 1, "default": 1, "resolution": 1, "help": "Bloqueo de rama izquierda si se desactiva."},
            "path7": {"label": "RBB -> RV", "unit": "on", "min": 0, "max": 1, "default": 1, "resolution": 1, "help": "Salida ventricular derecha."},
            "path8": {"label": "LBB -> LV", "unit": "on", "min": 0, "max": 1, "default": 1, "resolution": 1, "help": "Salida ventricular izquierda."},
        },
        "rhythms": ["Normal Sinus", "SA Node Failure"],
    },
    "pico_ecg_multimodel": {
        "name": "Multi-Model",
        "paper": "Quiroz-Juarez 2022",
        "desc": "4 selectable cardiac models",
        "leads": ["RA", "LA(I)", "LL(II)", "V1", "V2", "V3", "V4", "V5", "V6"],
        "num_leads": 9,
        "models": [
            "Heterogeneous 12-Lead",
            "Reaction-Diffusion (II)",
            "Ring Oscillators (II)",
            "Quasiperiodic (II)",
        ],
        "params": {
            "hr":    {"label": "Heart Rate",   "unit": "bpm",  "min": 15,   "max": 250,   "default": 72,   "resolution": 1},
            "amp":   {"label": "Amplitude",    "unit": "%",    "min": 0,    "max": 100,   "default": 100,  "resolution": 1},
            "a1":    {"label": "Alpha1 (P)",   "unit": "",     "min": 0,    "max": 2,     "default": 0.5,  "resolution": 0.01},
            "a2":    {"label": "Alpha2 (Ta)",  "unit": "",     "min": -2,   "max": 0,     "default": -0.2, "resolution": 0.01},
            "a3":    {"label": "Alpha3 (QRS)", "unit": "",     "min": 0,    "max": 5,     "default": 1.5,  "resolution": 0.01},
            "a4":    {"label": "Alpha4 (T)",   "unit": "",     "min": 0,    "max": 2,     "default": 0.3,  "resolution": 0.01},
            "delay": {"label": "Node Delay",   "unit": "ms",   "min": 0,    "max": 200,   "default": 35,   "resolution": 1},
        },
        "rhythms": [
            "Normal Sinus", "Sinus Bradycardia", "Sinus Tachycardia",
            "First Degree AVB", "Second Degree AVB Type I", "Second Degree AVB Type II",
            "Third Degree AVB", "Ventricular Tachycardia"
        ],
    },
}

LEAD_COLORS = ['#e74c3c', '#3498db', '#2ecc71', '#f39c12', '#9b59b6',
               '#1abc9c', '#e67e22', '#34495e', '#d35400']
LEAD_NAMES = ["RA", "LA(I)", "LL(II)", "V1", "V2", "V3", "V4", "V5", "V6"]

BAUD_RATE = 921600
SAMPLE_RATE = 2000
DEFAULT_DISPLAY_SECONDS = 4
MAX_DISPLAY_SECONDS = 30
DEFAULT_AMPLITUDE_SCALE = 2.0  # mV full-scale
# Max data points fed to matplotlib per line. Decimating the 2 kHz stream to
# ~2 points/pixel keeps the waveform sharp while making each repaint cheap.
# Without this the GUI froze (main thread wedged in draw) even though the Pico
# kept streaming happily.
MAX_RENDER_POINTS = 3000
# Capacity of the numpy rendering ring (matches the producer deque cap). At
# 2 kHz this holds >30 s; every channel column is preallocated so the steady
# state per frame is O(new samples + rendered points) with no object churn.
RING_CAP = MAX_DISPLAY_SECONDS * SAMPLE_RATE + 12000


# =============================================================================
# Design Tokens & Helpers
# =============================================================================

# Colour palette
C_BG     = '#EDF2F7'   # App / panel background
C_CARD   = '#FFFFFF'   # Card surface
C_BORDER = '#CBD5E1'   # Borders / separators
C_ACCENT = '#0D9488'   # Primary teal accent
C_TEXT   = '#1E293B'   # Primary text
C_TEXT2  = '#64748B'   # Secondary / dim text
C_BTN    = '#F1F5F9'   # Flat button background
C_BTN_HV = '#E2E8F0'   # Flat button hover
C_LED_OK = '#10B981'   # Connected status green
C_LED_NO = '#94A3B8'   # Disconnected status gray

# Fonts (Segoe UI — falls back to system sans-serif)
_FF  = 'Segoe UI'
FS   = (_FF, 8)               # small
FB   = (_FF, 9)               # body
FBB  = (_FF, 9, 'bold')       # body bold
FT   = (_FF, 10, 'bold')      # title


def _flat_btn(parent, text, command, bg=None, fg=None,
              font=None, abg=None, width=None):
    """tk.Button with flat card styling — replaces ttk.Button in the modern UI."""
    if bg is None:   bg  = C_BTN
    if fg is None:   fg  = C_TEXT
    if font is None: font = FB
    if abg is None:  abg  = C_BTN_HV
    kw = dict(text=text, command=command, bg=bg, fg=fg, font=font,
              relief='flat', bd=0, cursor='hand2',
              activebackground=abg, activeforeground=fg,
              padx=9, pady=5)
    if width:
        kw['width'] = width
    return tk.Button(parent, **kw)


# =============================================================================
# Serial Thread
# =============================================================================

class SerialThread(threading.Thread):
    def __init__(self, port, data_queue, cmd_queue, status_callback, info_callback,
                 pkt_callback=None, queue_lock=None):
        super().__init__(daemon=True)
        self.port = port
        self.data_queue = data_queue
        self.cmd_queue = cmd_queue
        self.status_callback = status_callback
        self.info_callback = info_callback
        self.pkt_callback = pkt_callback
        self.queue_lock = queue_lock or threading.Lock()
        self._stop_event = threading.Event()
        self.ser = None
        self.streaming = False
        # Number of floats per streamed frame. The Gaussian ECG+PPG project
        # sends 9 electrode potentials + 1 PPG channel; the other projects
        # send 9 electrode potentials only. The GUI updates this once the
        # project is detected (defaults to the ECG-only 9).
        self.num_channels = 9
        # Indices (within the frame) that carry pace-sense ADC voltages in
        # volts (0..3.3 V) rather than ECG/PPG. Empty for projects without a
        # pace front end (both bench projects stream them at frame indices 4-6).
        self.adc_indexes = ()
        # Sanity bound (+-mV) for the non-ADC channels. The Gaussian model
        # streams raw mV electrode potentials whose QRS peak can reach tens of
        # mV (R default 30 x projection), so a tight +-5 mV bound would drop
        # every QRS peak frame.
        self.mv_max = 50.0
        # When True the thread is collecting lines for a 'list' response.
        self.collecting_list = False
        self.total_samples_count = 0
        # Monotonic count of samples produced, incremented on every append.
        # Unlike len(data_queue) it survives the deque's popleft() trim, so the
        # render loop can tell "we absorbed this many of the producer's events"
        # without conflating production count with the bounded deque length.
        self.sample_seq = 0
        # Forward only a fraction of parsed frames to the terminal logger so
        # the ~2 kHz stream does not flood it (~20 frames/s here). The GUI
        # keeps its own sequence number for display.
        self._pkt_fwd_div = 100
        self._pkt_log_count = 0

    def stop(self):
        self._stop_event.set()
        # Closing the port here unblocks a pending serial read immediately;
        # waiting for the loop timeout alone can leave packaged instances
        # visible in Task Manager during shutdown.
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass

    def run(self):
        try:
            self.ser = serial.Serial(self.port, BAUD_RATE, timeout=0.1)
            time.sleep(2)
            self.status_callback("connected")
            self._send_cmd("ver")
        except Exception as e:
            self.status_callback(f"error: {e}")
            return

        while not self._stop_event.is_set():
            while not self.cmd_queue.empty():
                try:
                    cmd = self.cmd_queue.get_nowait()
                    self.ser.write((cmd + "\r\n").encode())
                    if "stream on" in cmd:
                        self.streaming = True
                    elif "stream off" in cmd:
                        self.streaming = False
                        # Give firmware time to print confirmation, then clear
                        # residual binary data so text parsing is clean.
                        time.sleep(0.15)
                        self.ser.reset_input_buffer()
                except serial.SerialException:
                    self.status_callback("disconnected")
                    self._close_serial()
                    return
                except Exception:
                    pass

            try:
                if self.streaming:
                    data_size = self.num_channels * 4
                    frame_size = data_size + 4
                    END_SYNC = b'\x55\xAA'

                    available = self.ser.in_waiting
                    if available >= frame_size:
                        raw = self.ser.read(available)
                        i = 0
                        while i <= len(raw) - frame_size:
                            if raw[i] == 0xAA and raw[i+1] == 0x55:
                                data = raw[i+2:i+2+data_size]
                                end = raw[i+2+data_size:i+2+data_size+2]
                                if len(data) == data_size and end == END_SYNC:
                                    fmt = '<%df' % self.num_channels
                                    values = list(struct.unpack(fmt, data))
                                    # Sanity bounds: ECG/PPG channels (raw mV)
                                    # within +-mv_max; pace-sense ADC channels
                                    # (V at the pin) within +-10 V.
                                    ok = True
                                    for ch_i, v in enumerate(values):
                                        limit = 10.0 if ch_i in self.adc_indexes else self.mv_max
                                        if not (-limit <= v <= limit):
                                            ok = False
                                            break
                                    if ok and len(values) == self.num_channels:
                                        with self.queue_lock:
                                            self.data_queue.append(values)
                                            self.total_samples_count += 1
                                            self.sample_seq += 1
                                            max_samples = MAX_DISPLAY_SECONDS * SAMPLE_RATE + 12000
                                            while len(self.data_queue) > max_samples:
                                                self.data_queue.popleft()
                                        if self.pkt_callback:
                                            self._pkt_log_count += 1
                                            if self._pkt_log_count % self._pkt_fwd_div == 0:
                                                self.pkt_callback(raw[i:i + frame_size], values)
                                i += frame_size
                            else:
                                i += 1
                else:
                    if self.ser.in_waiting > 0:
                        line = self.ser.readline().decode('utf-8', errors='replace').replace('\r', '').replace('\n', '').strip()
                        if line:
                            print(f"[RX] {line}")
                            self.info_callback(line)
            except serial.SerialException:
                # Port disappeared (device unplugged/reset) -> no busy-loop.
                self.status_callback("disconnected")
                self._close_serial()
                return
            except Exception:
                pass

        self._close_serial()
        self.status_callback("disconnected")

    def _close_serial(self):
        if self.ser and self.ser.is_open:
            try:
                self.ser.write(b"> stream off\r\n")
                time.sleep(0.05)
            except Exception:
                pass
            try:
                self.ser.close()
            except Exception:
                pass

    def _send_cmd(self, cmd):
        if self.ser and self.ser.is_open:
            try:
                self.ser.write((cmd + "\r\n").encode())
            except Exception:
                pass


# =============================================================================
# Custom Widgets
# =============================================================================

class ToggleSwitch(tk.Canvas):
    """Compact iOS-style toggle switch backed by a BooleanVar."""
    W, H = 36, 18

    def __init__(self, parent, variable, command=None, bg=C_BG, **kw):
        super().__init__(parent, width=self.W, height=self.H,
                         highlightthickness=0, bg=bg, **kw)
        self._var = variable
        self._cmd = command
        self._draw()
        self.bind('<Button-1>', self._click)
        self._var.trace_add('write', lambda *_: self.after_idle(self._draw))

    def _draw(self):
        self.delete('all')
        on  = bool(self._var.get())
        clr = C_ACCENT if on else C_LED_NO
        r   = self.H // 2
        # Track (two end-caps + centre rect)
        self.create_oval(0, 0, self.H, self.H, fill=clr, outline='')
        self.create_oval(self.W - self.H, 0, self.W, self.H, fill=clr, outline='')
        self.create_rectangle(r, 0, self.W - r, self.H, fill=clr, outline='')
        # Thumb
        m  = 2
        x0 = (self.W - self.H + m) if on else m
        self.create_oval(x0, m, x0 + self.H - 2*m, self.H - m,
                         fill='white', outline='')

    def _click(self, _=None):
        self._var.set(not bool(self._var.get()))
        if self._cmd:
            self._cmd()


class ToolTip:
    """Hover help: shows a small label near the widget after a short delay and
    dismisses it when the pointer leaves. Bind once with ToolTip(widget, text)."""

    def __init__(self, widget, text, delay_ms=450):
        self.widget = widget
        self.text = text
        self.delay = delay_ms
        self._after = None
        self._tip = None
        widget.bind('<Enter>', self._schedule, add='+')
        widget.bind('<Leave>', self._hide, add='+')
        widget.bind('<ButtonPress>', self._hide, add='+')

    def _schedule(self, _=None):
        self._hide()
        self._after = self.widget.after(self.delay, self._show)

    def _show(self):
        self._after = None
        try:
            x = self.widget.winfo_rootx()
            y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
            tip = tk.Toplevel(self.widget)
            tip.wm_overrideredirect(True)
            tip.wm_geometry(f"+{x}+{y}")
            lbl = tk.Label(tip, text=self.text, justify=tk.LEFT,
                           background='#1e293b', foreground='#f1f5f9',
                           font=('Segoe UI', 9), wraplength=300,
                           padx=8, pady=6, borderwidth=1, relief='solid')
            lbl.pack()
            # Keep it on top and never grab focus from the main window.
            tip.transient(self.widget.winfo_toplevel())
            tip.attributes('-topmost', True)
            self._tip = tip
            self._tip.bind('<Leave>', lambda e: self._hide())
        except Exception:
            self._tip = None

    def _hide(self, _=None):
        if self._after is not None:
            self.widget.after_cancel(self._after)
            self._after = None
        if self._tip is not None:
            try:
                self._tip.destroy()
            except Exception:
                pass
            self._tip = None


# =============================================================================
# Main Application
# =============================================================================

class ECGApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ECG Streaming GUI")
        self.root.geometry("1500x950")
        self.root.minsize(1100, 650)

        self.serial_thread = None
        self.data_queue = deque()
        self.cmd_queue = queue.Queue()
        # Guards the producer (serial thread) / consumer (Tk main loop) shared
        # deque. It was mutated while iterated, which raised "deque mutated
        # during iteration" and wedged repaints.
        self.queue_lock = threading.Lock()
        # Numpy rolling ring used by the renderer. _ring_n is the monotonic
        # number of samples absorbed (wraps every RING_CAP). Lazily allocated
        # on first data so the layout matches the detected project.
        self._ring = None
        self._ring_n = 0
        self.connected = False
        self.project_type = None
        self.project_info = {}
        self.streaming = False
        self.num_leads = 9
        self.lead_names = list(LEAD_NAMES)
        self.visible_leads = [True] * 9
        self.slider_vars = {}
        self.slider_widgets = {}
        self.ppg_index = None
        # Plot refresh state: we only redraw when new samples arrived or the
        # view state changed, so the UI thread stays free for scroll/click.
        self._plot_dirty = True
        self._plotted_len = 0
        # pace_sim state: True when the pacemaker test-bench firmware is active
        self.pace_mode = False
        self.pace_indexes = None
        self.cellmodel_row = None
        self.cenelecpol_row = None
        self._pace_command_capture = False
        self._pace_command_stream_was_on = False

        # pico_ecg file-list state
        self._pico_ecg_list_lines = []   # raw lines accumulated from 'list'
        self._pico_ecg_collecting = False  # True while parsing 'list' output
        self._pico_ecg_files = []        # list of (index, display_name) tuples

        self.display_seconds = DEFAULT_DISPLAY_SECONDS
        self.amplitude_scale = DEFAULT_AMPLITUDE_SCALE
        self.auto_scale = True
        self.sample_rate = 2000

        # --- Recording state ---
        self._recording = False
        self._record_buf = []   # list of rows [t, ch0..ch8 (,ppg)]
        self._record_t0 = 0.0

        # --- Terminal monitor (raw stream + parsed frames) ---
        self.terminal_queue = queue.Queue()
        self.terminal_buffer = deque(maxlen=2000)   # ('raw', seq, bytes) | ('dec', seq, values) | ('txt', str)
        self.terminal_win = None
        self.terminal_text = None
        self.terminal_paused = False
        self.term_full_hex = False
        self.term_autoscroll = True
        self._term_seq = 0

        # --- Real-time HR estimation ---
        self._hr_measured = 0          # last estimated BPM
        self._last_hr_t = 0.0        # monotonic clock of last HR pass (1 Hz cap)
        self._hr_peak_times = deque(maxlen=8)  # monotonic timestamps of R-peaks
        self._hr_prev_peak_sidx = -9999        # sample index of last detected peak
        self._hr_prev_peak_val = -999.0

        # --- Throughput stats ---
        self._total_samples = 0
        self._last_stat_time = time.monotonic()
        self._last_stat_samples = 0
        self._samples_per_sec = 0

        self._setup_styles()
        self._build_ui()
        self._start_plot_update()
        self._update_stats()

    # ------------------------------------------------------------------
    # Visual setup helpers
    # ------------------------------------------------------------------

    def _setup_styles(self):
        """Configure ttk theme and root background for the modern card look."""
        self.root.configure(bg=C_BG)
        s = ttk.Style()
        s.theme_use('clam')
        s.configure('TFrame',      background=C_BG)
        s.configure('TLabel',      background=C_BG, foreground=C_TEXT, font=FB)
        s.configure('TCombobox',
                    fieldbackground=C_CARD, background=C_BTN,
                    foreground=C_TEXT, font=FB,
                    bordercolor=C_BORDER, lightcolor=C_BORDER, darkcolor=C_BORDER,
                    arrowcolor=C_TEXT2, selectbackground=C_ACCENT,
                    selectforeground='white', insertcolor=C_TEXT)
        s.map('TCombobox',
              fieldbackground=[('readonly', C_CARD)],
              bordercolor=[('focus', C_ACCENT)])
        s.configure('TScale',
                    background=C_CARD, troughcolor='#E2E8F0',
                    sliderlength=16, sliderrelief='flat', borderwidth=0)
        s.map('TScale', background=[('active', C_CARD)])
        s.configure('TCheckbutton',
                    background=C_CARD, foreground=C_TEXT, font=FB,
                    focuscolor=C_CARD)
        s.map('TCheckbutton',
              background=[('active', C_CARD)],
              indicatorcolor=[('selected', C_ACCENT), ('!selected', C_BTN)])
        s.configure('Vertical.TScrollbar',
                    background=C_BORDER, troughcolor=C_BG,
                    arrowcolor=C_TEXT2, borderwidth=0, relief='flat')
        s.configure('TSeparator', background=C_BORDER)

    def _make_card(self, parent, title):
        """Return (outer_frame, content_frame) with white card + border styling."""
        outer = tk.Frame(parent, bg=C_BG)
        inner = tk.Frame(outer, bg=C_CARD,
                         highlightbackground=C_BORDER, highlightthickness=1)
        inner.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)
        # Title bar with accent left stripe
        hdr = tk.Frame(inner, bg=C_CARD)
        hdr.pack(fill=tk.X, padx=8, pady=(8, 4))
        tk.Frame(hdr, bg=C_ACCENT, width=3).pack(side=tk.LEFT, fill=tk.Y)
        tk.Label(hdr, text=title, bg=C_CARD, fg=C_TEXT,
                 font=FT, padx=6).pack(side=tk.LEFT)
        # Hairline separator
        tk.Frame(inner, bg=C_BORDER, height=1).pack(fill=tk.X, padx=8)
        # Content area
        content = tk.Frame(inner, bg=C_CARD)
        content.pack(fill=tk.BOTH, expand=True, padx=10, pady=8)
        return outer, content

    def _build_ui(self):
        self._build_menubar()

        root_container = tk.Frame(self.root, bg=C_BG)
        root_container.pack(fill=tk.BOTH, expand=True)

        self._build_top_header(root_container)

        main = tk.Frame(root_container, bg=C_BG)
        main.pack(fill=tk.BOTH, expand=True)
        main.columnconfigure(0, weight=2, minsize=400)
        main.columnconfigure(1, weight=3, minsize=400)
        main.rowconfigure(0, weight=1)

        left_frame = tk.Frame(main, bg=C_BG)
        left_frame.grid(row=0, column=0, sticky='nsew')

        right_frame = tk.Frame(main, bg=C_CARD)
        right_frame.grid(row=0, column=1, sticky='nsew')

        self._build_left_panel(left_frame)
        self._build_plot(right_frame)
        self._build_statusbar(self.root)

    def _build_menubar(self):
        menubar = tk.Menu(self.root)

        # File menu
        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="⏺ Grabación CSV", command=self._toggle_recording)
        file_menu.add_command(label="📷 Guardar Snapshot PNG", command=self._save_snapshot)
        file_menu.add_separator()
        file_menu.add_command(label="Salir", command=self._shutdown)
        menubar.add_cascade(label="Archivo", menu=file_menu)

        # View menu
        view_menu = tk.Menu(menubar, tearoff=0)
        view_menu.add_command(label="Limpiar Gráfico", command=self._clear_plot)
        view_menu.add_command(label="Alternar Auto-Escala", command=self._on_auto_scale_toggle)
        menubar.add_cascade(label="Ver", menu=view_menu)

        # Terminal menu (inspector for the raw binary stream + parsed frames)
        term_menu = tk.Menu(menubar, tearoff=0)
        term_menu.add_command(label="🖥️ Abrir Terminal de Datos", command=self._open_terminal)
        term_menu.add_command(label="🗑 Limpiar Buffer y Pantalla", command=self._terminal_clear_all)
        menubar.add_cascade(label="Terminal", menu=term_menu)

        # Help menu
        help_menu = tk.Menu(menubar, tearoff=0)
        help_menu.add_command(label="📘 Manual de Uso Completo", command=lambda: self._show_help_dialog(0))
        help_menu.add_command(label="🔬 Detección de Modelos", command=lambda: self._show_help_dialog(1))
        help_menu.add_command(label="⚡ Play/Stop vs Stream ON/OFF", command=lambda: self._show_help_dialog(2))
        help_menu.add_command(label="🫀 Medidor de Frecuencia Cardíaca (HR)", command=lambda: self._show_help_dialog(3))
        help_menu.add_separator()
        help_menu.add_command(label="ℹ️ Acerca de Fantomas ECG v1.2.0", command=self._show_about_dialog)
        menubar.add_cascade(label="Ayuda", menu=help_menu)

        self.root.config(menu=menubar)

    def _build_top_header(self, parent):
        hdr = tk.Frame(parent, bg=C_CARD, highlightbackground=C_BORDER, highlightthickness=1)
        hdr.pack(fill=tk.X, side=tk.TOP, padx=6, pady=(6, 2))

        left_sub = tk.Frame(hdr, bg=C_CARD)
        left_sub.pack(side=tk.LEFT, padx=10, pady=6)

        tk.Label(left_sub, text="🫀 Fantomas ECG", font=(_FF, 12, 'bold'), bg=C_CARD, fg=C_ACCENT).pack(side=tk.LEFT)
        tk.Label(left_sub, text="Control Panel & Real-Time Monitor", font=FB, bg=C_CARD, fg=C_TEXT).pack(side=tk.LEFT, padx=(6, 8))

        ver_lbl = tk.Label(left_sub, text="v1.2.0", font=FS, bg=C_BTN, fg=C_TEXT2, padx=6, pady=1)
        ver_lbl.pack(side=tk.LEFT)

        right_sub = tk.Frame(hdr, bg=C_CARD)
        right_sub.pack(side=tk.RIGHT, padx=8, pady=4)

        _flat_btn(right_sub, '📘 Manual & Ayuda', lambda: self._show_help_dialog(0),
                  bg=C_BTN, fg=C_TEXT, font=FB).pack(side=tk.LEFT, padx=3)
        _flat_btn(right_sub, '🔬 Modelos', lambda: self._show_help_dialog(1),
                  bg=C_BTN, fg=C_TEXT, font=FB).pack(side=tk.LEFT, padx=3)
        _flat_btn(right_sub, '⚡ Play vs Stream', lambda: self._show_help_dialog(2),
                  bg=C_BTN, fg=C_TEXT, font=FB).pack(side=tk.LEFT, padx=3)
        _flat_btn(right_sub, 'ℹ️ Acerca de', self._show_about_dialog,
                  bg=C_BTN, fg=C_TEXT, font=FB).pack(side=tk.LEFT, padx=3)

    def _show_help_dialog(self, initial_tab=0):
        dlg = tk.Toplevel(self.root)
        dlg.title("Guía de Uso y Ayuda - Fantomas ECG Control Panel v1.2.0")
        dlg.geometry("900x650")
        dlg.minsize(700, 500)
        dlg.transient(self.root)
        dlg.grab_set()

        # Header
        hdr = tk.Frame(dlg, bg=C_ACCENT, pady=12, padx=16)
        hdr.pack(fill=tk.X)
        tk.Label(hdr, text="📘 Manual de Usuario y Documentación de Fantomas ECG",
                 font=(_FF, 13, 'bold'), bg=C_ACCENT, fg='white').pack(anchor=tk.W)
        tk.Label(hdr, text="Guía interactiva para la configuración, detección de firmware y streaming en tiempo real.",
                 font=FB, bg=C_ACCENT, fg='#E6FFFA').pack(anchor=tk.W)

        # Notebook
        nb = ttk.Notebook(dlg)
        nb.pack(fill=tk.BOTH, expand=True, padx=12, pady=12)

        # Helper to create scrollable tab content
        def _make_scrollable_tab(title):
            tab = ttk.Frame(nb)
            nb.add(tab, text=title)
            canvas = tk.Canvas(tab, bg='white', highlightthickness=0)
            sb = ttk.Scrollbar(tab, orient=tk.VERTICAL, command=canvas.yview)
            body = tk.Frame(canvas, bg='white', padx=16, pady=16)
            body.bind('<Configure>', lambda e: canvas.configure(scrollregion=canvas.bbox('all')))
            canvas.create_window((0, 0), window=body, anchor='nw')
            canvas.configure(yscrollcommand=sb.set)
            canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            sb.pack(side=tk.RIGHT, fill=tk.Y)
            canvas.bind('<MouseWheel>', lambda e: canvas.yview_scroll(int(-1*(e.delta/120)), 'units'))
            return body

        # Tab 0: Guía de Uso General
        t0 = _make_scrollable_tab("🚀 Guía Rápida")
        
        def _add_section(parent, title, text_content):
            sf = tk.Frame(parent, bg='white', pady=6)
            sf.pack(fill=tk.X)
            tk.Label(sf, text=title, font=(_FF, 11, 'bold'), bg='white', fg=C_TEXT).pack(anchor=tk.W, pady=(0, 4))
            tk.Message(sf, text=text_content, font=FB, bg='white', fg=C_TEXT, width=800).pack(anchor=tk.W)
            tk.Frame(parent, bg=C_BORDER, height=1).pack(fill=tk.X, pady=8)

        _add_section(t0, "1. Conexión Serial con la Raspberry Pi Pico",
                     "1. Conecte la placa Raspberry Pi Pico por USB al sistema.\n"
                     "2. Seleccione el puerto COM correspondiente en la sección 'Connection' del panel izquierdo (o presione ⟳ para actualizar la lista).\n"
                     "3. Presione el botón 'Connect'. La GUI establecerá la comunicación a 115200 baudios.")

        _add_section(t0, "2. Detección Automática de Modelo (Handshake)",
                     "Al conectar, la interfaz envía automáticamente el comando CLI 'ver' a la placa. El firmware responde identificando el tipo de proyecto activo (Conduction Model, Gaussian ECG+PPG, Multi-Model o pico_ecg stored signals).\n"
                     "La GUI reconfigura automáticamente sus deslizadores de parámetros, ritmos e interfaces específicas de acuerdo al firmware detectado.")

        _add_section(t0, "3. Visualización y Ajustes en Tiempo Real",
                     "• Escala Horizontal (Display Time): Ajuste la ventana temporal visible entre 1 y 30 segundos.\n"
                     "• Escala Vertical (Amplitude Scale): Modifique la ganancia (mV full-scale) o active Auto-Scale para ajuste dinámico de ejes.\n"
                     "• Derivaciones Visibles: Marque o desmarque individualmente las derivadas RA, LA(I), LL(II), V1-V6 o el canal PPG para aislar señales.")

        _add_section(t0, "4. Grabación a CSV y Captura PNG",
                     "• Botón ⏺ Record CSV: Inicia la grabación continua de todas las muestras recibidas con timestamps microsegundos a un archivo `.csv` exportable.\n"
                     "• Botón 📷 Save PNG: Genera una imagen vectorial/rasterizada limpia de 150 DPI de las formas de onda desplegadas actualmente en pantalla.")

        # Tab 1: Detección de Modelos
        t1 = _make_scrollable_tab("🔬 Modelos de Firmware")

        _add_section(t1, "1. Conduction Model (Quiroz-Juárez 2018)",
                     "Modelo electrofisiológico basado en osciladores acoplados van der Pol y FitzHugh-Nagumo para los nodos SA, AV y el sistema His-Purkinje.\n"
                     "• Parámetros dinámicos: Alpha1 (P), Alpha2 (Ta), Alpha3 (QRS), Alpha4 (T), Node Delay, Baseline Amp/Freq, Heart Rate (BPM).\n"
                     "• Ritmos incluidos: Sinusal normal, Bradicardia, Taquicardia, Bloqueos AV Grado I, II (Tipo I/II) y III, Taquicardia Ventricular.")

        _add_section(t1, "2. Gaussian ECG + PPG Model (Huynh/Tran 2026)",
                     "Modelo sintético de generación de ondas por suma de gaussianas para 9 derivaciones ECG acoplado a un sintetizador de fotopletismografía (PPG) de 5 componentes gaussianas.\n"
                     "• Parámetros dinámicos: PR Interval, RSA Depth (Arritmia Sinusal Respiratoria), RSA Freq, R/T/P Amplitudes, QT Exponent.\n"
                     "• Canal PPG: Muestra la curva de pulso óptico alineada temporalmente con la onda R electrocardíaca.")

        _add_section(t1, "3. Multi-Model (Quiroz-Juárez 2022)",
                     "Sistemas multisintéticos avanzados con cuatro formulaciones matemáticas seleccionables:\n"
                     "1) Heterogeneous 12-Lead, 2) Reaction-Diffusion, 3) Ring Oscillators, 4) Quasiperiodic.")

        _add_section(t1, "4. pico_ecg (Reproductor de Señales Almacenadas)",
                     "Firmware diseñado para reproducir registros clínicos reales (archivos CSV/DAT) almacenados en la memoria flash SPI de la Pico.\n"
                     "La GUI solicita la lista mediante el comando 'list', muestra los archivos en un desplegable y permite seleccionar y cambiar de señal al instante ('play N').")

        # Tab 2: Stream ON/OFF vs Play/Stop
        t2 = _make_scrollable_tab("⚡ Play/Stop vs Stream ON/OFF")

        _add_section(t2, "📡 Stream ON / Stream OFF (Comandos Serial Hardware)",
                     "• Stream ON: Envía el comando CLI '> stream on' por el puerto serie. La Raspberry Pi Pico activa la interrupción del temporizador a 2000 Hz y comienza a transmitir paquetes binarios (4 bytes float por canal + cabecera 0xAA 0x55 + checksum 0x55 0xAA).\n"
                     "• Stream OFF: Envía '> stream off'. La placa detiene el temporizador de emisión de datos binarios y regresa al modo de consola de comandos texto.")

        _add_section(t2, "⏯️ Play / Pause (Control Renderizador GUI)",
                     "• Play (Reanudar): Permite que el bucle gráfico actualice el canvas con las muestras entrantes de la cola en tiempo real.\n"
                     "• Pause / Stop (Detener Render): Congela las curvas desplegadas en pantalla sin cortar la conexión serial ni apagar el streaming de la placa. Es ideal para congelar eventos transitorios, inspeccionar latidos con detenimiento o exportar capturas PNG.")

        _add_section(t2, "💡 Cuándo usar cada control",
                     "• Si desea cambiar un parámetro del modelo en el firmware: Utilice los sliders del panel izquierdo (envían comandos CLI inmediatos).\n"
                     "• Si desea pausar la vista sin desconectar: Use el botón 'Pause' de la barra de herramientas del gráfico.\n"
                     "• Si desea detener la transmisión serie por completo para liberar el bus: Presione 'Stream OFF'.")

        # Tab 3: HR & Stats
        t3 = _make_scrollable_tab("🫀 Frecuencia Cardíaca & Stats")

        _add_section(t3, "Cálculo de Frecuencia Cardíaca en Tiempo Real (HR)",
                     "• Algoritmo de Detección: La GUI analiza continuamente la derivada de la señal LL(II) buscando deflexiones R superiores al umbral dinámico adaptativo.\n"
                     "• Periodo Refractario: Aplica un filtro digital refractario de 200 ms para evitar contajes dobles por ondas T picudas o artefactos de alta frecuencia.\n"
                     "• Despliegue: El valor instantáneo en BPM (Beats Per Minute) se muestra tanto en la barra de estado inferior como en la esquina superior derecha del gráfico ECG.")

        _add_section(t3, "Barra de Estado e Indicadores de Rendimiento",
                     "• Puerto Serie: Muestra el puerto activo y el estado de conexión ('Connected (streaming)', 'Connected (menu)', etc.).\n"
                     "• Throughput (samp/s): Indica la tasa de muestreo efectiva recibida en el PC (~2000 muestras/segundo por canal).\n"
                     "• Indicador ⏺ REC: Cuando la grabación CSV está en curso, parpadea y contabiliza el tiempo exacto de grabación en segundos.")

        # Select target tab
        nb.select(initial_tab)

        # Footer button
        ftr = tk.Frame(dlg, bg='white', pady=10, padx=16)
        ftr.pack(fill=tk.X, side=tk.BOTTOM)
        _flat_btn(ftr, 'Entendido / Cerrar', dlg.destroy, bg=C_ACCENT, fg='white', font=FBB).pack(side=tk.RIGHT)

    def _show_about_dialog(self):
        dlg = tk.Toplevel(self.root)
        dlg.title("Acerca de Fantomas ECG Control Panel")
        dlg.geometry("560x430")
        dlg.resizable(False, False)
        dlg.transient(self.root)
        dlg.grab_set()

        content = tk.Frame(dlg, bg=C_CARD, padx=24, pady=24)
        content.pack(fill=tk.BOTH, expand=True)

        tk.Label(content, text="🫀 Fantomas ECG Control Panel", font=(_FF, 14, 'bold'), bg=C_CARD, fg=C_ACCENT).pack(anchor=tk.W, pady=(0, 2))
        tk.Label(content, text="Versión 1.2.0 — Plataforma de Electrocardiografía Sintética", font=FBB, bg=C_CARD, fg=C_TEXT).pack(anchor=tk.W, pady=(0, 8))

        tk.Frame(content, bg=C_BORDER, height=1).pack(fill=tk.X, pady=(0, 10))

        info_text = (
            "Sistema de monitoreo, simulación y control en tiempo real para placas "
            "Raspberry Pi Pico con firmware Fantomas ECG.\n\n"
            "• Soporte Multiproyecto: Conduction Model (Quiroz-Juárez 2018), "
            "Gaussian ECG+PPG (Huynh/Tran 2026), Multi-Model (Quiroz-Juárez 2022) y "
            "pico_ecg (Signal Player LUDB).\n"
            "• Adquisición Serie: Transmisión binaria flotante de alta velocidad por USB CDC.\n"
            "• Herramientas: Detección R-R de HR en tiempo real, grabación CSV y exportación PNG.\n\n"
            "🏛️ Desarrollado en el Laboratorio de Prototipado Electrónico y 3D\n"
            "FIUNER — Facultad de Ingeniería, Universidad Nacional de Entre Ríos\n"
            "🇦🇷 Entre Ríos, Argentina."
        )
        tk.Message(content, text=info_text, font=FB, bg=C_CARD, fg=C_TEXT2, width=500).pack(anchor=tk.W, pady=(0, 14))

        ftr = tk.Frame(content, bg=C_CARD)
        ftr.pack(fill=tk.X, side=tk.BOTTOM)
        tk.Label(ftr, text="© 2026 Laboratorio de Prototipado Electrónico y 3D — FIUNER", font=FS, bg=C_CARD, fg=C_TEXT2).pack(side=tk.LEFT)
        _flat_btn(ftr, 'Cerrar', dlg.destroy, bg=C_BTN, fg=C_TEXT, font=FBB).pack(side=tk.RIGHT)

    def _build_left_panel(self, parent):
        canvas = tk.Canvas(parent, bg=C_BG, highlightthickness=0)
        scrollbar = ttk.Scrollbar(parent, orient=tk.VERTICAL, command=canvas.yview)
        self.left_frame = tk.Frame(canvas, bg=C_BG)

        self.left_frame.bind(
            '<Configure>',
            lambda e: canvas.configure(scrollregion=canvas.bbox('all'))
        )
        canvas.create_window((0, 0), window=self.left_frame, anchor='nw')
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        canvas.bind('<MouseWheel>',
                    lambda e: canvas.yview_scroll(int(-1*(e.delta/120)), 'units'))

        self._build_connection_section(self.left_frame)
        self._build_info_section(self.left_frame)
        self._build_display_section(self.left_frame)
        self._build_params_section(self.left_frame)
        self._build_rhythm_section(self.left_frame)
        self._build_pico_ecg_section(self.left_frame)
        self._build_pace_bench_section(self.left_frame)
        self._build_pace_detect_section(self.left_frame)
        self._build_device_section(self.left_frame)
        self._build_leads_section(self.left_frame)

    def _build_connection_section(self, parent):
        outer, card = self._make_card(parent, 'Connection')
        outer.pack(fill=tk.X, padx=6, pady=(6, 3))

        # Port selector row
        pr = tk.Frame(card, bg=C_CARD)
        pr.pack(fill=tk.X, pady=(0, 6))
        tk.Label(pr, text='Port', bg=C_CARD, fg=C_TEXT2, font=FS,
                 width=4, anchor=tk.W).pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(pr, textvariable=self.port_var,
                                        state='readonly', font=FB)
        self.port_combo.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 4))
        _flat_btn(pr, '⟳', self._refresh_ports,
                  bg=C_CARD, fg=C_TEXT2, font=(_FF, 13, 'bold'),
                  abg=C_BTN).pack(side=tk.LEFT, padx=(0, 3))
        self.connect_btn = _flat_btn(pr, 'Connect', self._toggle_connect,
                                      bg=C_ACCENT, fg='white', font=FBB,
                                      abg='#0F766E')
        self.connect_btn.pack(side=tk.LEFT)

        # Status row with LED indicator
        sr = tk.Frame(card, bg=C_CARD)
        sr.pack(fill=tk.X)
        self._status_led = tk.Canvas(sr, width=10, height=10,
                                      bg=C_CARD, highlightthickness=0)
        self._status_led.pack(side=tk.LEFT, padx=(0, 5), pady=1)
        self._led_oval = self._status_led.create_oval(1, 1, 9, 9,
                                                        fill=C_LED_NO, outline='')
        self.status_label = tk.Label(sr, text='Disconnected',
                                      bg=C_CARD, fg=C_TEXT2, font=FS)
        self.status_label.pack(side=tk.LEFT)
        self._refresh_ports()

    def _build_info_section(self, parent):
        outer, card = self._make_card(parent, 'Device Info')
        outer.pack(fill=tk.X, padx=6, pady=3)
        self.info_frame = outer  # kept for external pack_forget calls

        self.info_text = tk.Text(card, height=3, state=tk.DISABLED,
                                  font=('Consolas', 8), bg='#F8FAFC',
                                  fg=C_TEXT, relief='flat', wrap=tk.WORD,
                                  highlightthickness=1,
                                  highlightbackground=C_BORDER)
        self.info_text.pack(fill=tk.X, pady=(0, 6))

        br = tk.Frame(card, bg=C_CARD)
        br.pack(fill=tk.X)
        _flat_btn(br, '⚙  Get Settings', self._get_settings,
                  bg=C_BTN, fg=C_TEXT, font=FB).pack(side=tk.LEFT, padx=(0, 6))
        _flat_btn(br, '✕  Clear Log', self._clear_log,
                  bg=C_BTN, fg=C_TEXT2, font=FB).pack(side=tk.LEFT)

    def _build_display_section(self, parent):
        outer, card = self._make_card(parent, 'Display Controls')
        outer.pack(fill=tk.X, padx=6, pady=3)

        def _srow(label, var, frm, to, cmd):
            row = tk.Frame(card, bg=C_CARD)
            row.pack(fill=tk.X, pady=2)
            tk.Label(row, text=label, bg=C_CARD, fg=C_TEXT2, font=FS,
                     width=13, anchor=tk.W).pack(side=tk.LEFT)
            vl = tk.Label(row, bg=C_CARD, fg=C_TEXT, font=FBB,
                          width=8, anchor=tk.E)
            vl.pack(side=tk.RIGHT)
            sl = ttk.Scale(row, variable=var, from_=frm, to=to,
                           orient=tk.HORIZONTAL, command=cmd)
            sl.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)
            return sl, vl

        self.time_var = tk.DoubleVar(value=DEFAULT_DISPLAY_SECONDS)
        self.time_slider, self.time_label = _srow(
            'Time window', self.time_var, 0.5, MAX_DISPLAY_SECONDS,
            self._on_time_change)
        self.time_label.config(text=f'{DEFAULT_DISPLAY_SECONDS:.1f} s')
        ToolTip(self.time_slider, 'Ventana temporal visible (segundos) en el '
                                  'gráfico. Menos segundos = traza más amplia '
                                  'y detallada; más segundos = mayor '
                                  'historia visible.')

        self.amp_scale_var = tk.DoubleVar(value=DEFAULT_AMPLITUDE_SCALE)
        self.amp_scale_slider, self.amp_scale_label = _srow(
            'Amplitude', self.amp_scale_var, 0.1, 10.0,
            self._on_amp_scale_change)
        self.amp_scale_label.config(text=f'{DEFAULT_AMPLITUDE_SCALE:.1f} mV')
        ToolTip(self.amp_scale_slider, 'Escala vertical fija del gráfico (mV '
                                       'pico). Usado cuando Auto-scale Y está '
                                       'OFF; súbelo para ver el rango completo '
                                       'de la señal.')

        # Auto-scale Y toggle switch
        tog = tk.Frame(card, bg=C_CARD)
        tog.pack(fill=tk.X, pady=(6, 0))
        autoscale_tt = ('Auto-escalado del eje Y: ON ajusta el rango vertical '
                        'a la amplitud de la señal visible; OFF lo fija en '
                        '+/-Amplitude. Útil para inspeccionar señales '
                        'pequeñas.')
        autoscale_lbl = tk.Label(tog, text='Auto-scale Y', bg=C_CARD,
                                 fg=C_TEXT, font=FB)
        autoscale_lbl.pack(side=tk.LEFT)
        ToolTip(autoscale_lbl, autoscale_tt)
        self.auto_scale_var = tk.BooleanVar(value=True)
        autoscale_tog = ToggleSwitch(tog, self.auto_scale_var,
                                     command=self._on_auto_scale_toggle,
                                     bg=C_CARD)
        autoscale_tog.pack(side=tk.LEFT, padx=(8, 0))
        ToolTip(autoscale_tog, autoscale_tt)

    def _build_params_section(self, parent):
        self.params_outer, content = self._make_card(parent, 'Model Parameters')
        self.params_outer.pack(fill=tk.X, padx=6, pady=3)
        self.params_frame = content  # content area — used by _setup_project_ui

        pf = tk.Frame(content, bg=C_CARD)
        pf.pack(fill=tk.X, pady=(0, 4))
        _flat_btn(pf, '▶  Play', self._send_play,
                  bg='#1D4ED8', fg='white', font=FBB,
                  abg='#1E40AF').pack(side=tk.LEFT, padx=(0, 4))
        _flat_btn(pf, '■  Stop', self._send_stop,
                  bg='#DC2626', fg='white', font=FBB,
                  abg='#B91C1C').pack(side=tk.LEFT, padx=(0, 4))
        _flat_btn(pf, '⚙  Apply', self._apply_all_params,
                  bg=C_BTN, fg=C_TEXT, font=FBB).pack(side=tk.LEFT, padx=(0, 4))
        self.stream_btn = _flat_btn(pf, 'Stream ON', self._toggle_stream,
                                     bg=C_ACCENT, fg='white', font=FBB,
                                     abg='#0F766E')
        self.stream_btn.config(state=tk.DISABLED)
        self.stream_btn.pack(side=tk.LEFT)

    def _build_rhythm_section(self, parent):
        self.rhythm_outer, card = self._make_card(parent, 'Rhythm Preset')
        self.rhythm_outer.pack(fill=tk.X, padx=6, pady=3)
        # Keep self.rhythm_frame pointing to the OUTER for pack_forget compatibility
        self.rhythm_frame = self.rhythm_outer

        self.rhythm_var = tk.StringVar()
        self.rhythm_combo = ttk.Combobox(card, textvariable=self.rhythm_var,
                                          state='readonly', font=FB)
        self.rhythm_combo.pack(fill=tk.X)
        self.rhythm_combo.bind('<<ComboboxSelected>>', self._on_rhythm_change)

    def _build_pico_ecg_section(self, parent):
        """Panel for pico_ecg: ECG file selector from the device."""
        self.pico_ecg_frame, card = self._make_card(parent, 'ECG Signal — pico_ecg')
        # Initially hidden; shown only when pico_ecg firmware is detected

        # Signal file selector
        file_row = tk.Frame(card, bg=C_CARD)
        file_row.pack(fill=tk.X, pady=(0, 4))
        tk.Label(file_row, text='Signal', bg=C_CARD, fg=C_TEXT2,
                 font=FS).pack(side=tk.LEFT)
        self.pico_ecg_file_var = tk.StringVar(value='(not loaded)')
        self.pico_ecg_file_combo = ttk.Combobox(
            file_row, textvariable=self.pico_ecg_file_var,
            state='readonly', font=FB)
        self.pico_ecg_file_combo.pack(side=tk.LEFT, padx=(4, 0),
                                       fill=tk.X, expand=True)
        self.pico_ecg_file_combo.bind('<<ComboboxSelected>>',
                                       self._on_pico_ecg_file_change)

        # Refresh + status
        br = tk.Frame(card, bg=C_CARD)
        br.pack(fill=tk.X, pady=(2, 4))
        self.pico_ecg_refresh_btn = _flat_btn(
            br, '⟳  Refresh List', self._pico_ecg_request_list,
            bg=C_BTN, fg=C_TEXT, font=FB)
        self.pico_ecg_refresh_btn.pack(side=tk.LEFT, padx=(0, 6))
        self.pico_ecg_status_lbl = tk.Label(br, text='', bg=C_CARD,
                                             fg=C_TEXT2, font=FS)
        self.pico_ecg_status_lbl.pack(side=tk.LEFT)

        # Play / Stop / Stream
        cr = tk.Frame(card, bg=C_CARD)
        cr.pack(fill=tk.X, pady=(0, 6))
        _flat_btn(cr, '▶  Play', self._send_play,
                  bg='#1D4ED8', fg='white', font=FBB,
                  abg='#1E40AF').pack(side=tk.LEFT, padx=(0, 4))
        _flat_btn(cr, '■  Stop', self._send_stop,
                  bg='#DC2626', fg='white', font=FBB,
                  abg='#B91C1C').pack(side=tk.LEFT, padx=(0, 4))
        self.pico_ecg_stream_btn = _flat_btn(
            cr, 'Stream ON', self._pico_ecg_toggle_stream,
            bg=C_ACCENT, fg='white', font=FBB, abg='#0F766E')
        self.pico_ecg_stream_btn.pack(side=tk.LEFT, padx=(0, 4))
        _flat_btn(cr, 'Get Settings', self._get_settings,
                  bg=C_BTN, fg=C_TEXT, font=FB).pack(side=tk.LEFT)

        # HR / Amplitude sliders
        for param_key, label, unit, from_, to_, default in [
            ('hr',  'Heart Rate', 'bpm', 1,   300, 72),
            ('amp', 'Amplitude',  '%',   0,   100, 100),
        ]:
            row = tk.Frame(card, bg=C_CARD)
            row.pack(fill=tk.X, pady=2)
            tk.Label(row, text=label, bg=C_CARD, fg=C_TEXT2, font=FS,
                     width=12, anchor=tk.W).pack(side=tk.LEFT)
            var = tk.DoubleVar(value=default)
            self.slider_vars[param_key] = var
            lbl = tk.Label(row, text=f'{default} {unit}', bg=C_CARD,
                           fg=C_TEXT, font=FBB, width=9, anchor=tk.E)
            lbl.pack(side=tk.RIGHT)
            _flat_btn(row, 'Set', lambda k=param_key: self._pico_ecg_apply_param(k),
                      bg=C_BTN, fg=C_TEXT, font=FS).pack(side=tk.RIGHT, padx=(3, 0))
            scale = ttk.Scale(row, variable=var, from_=from_, to=to_,
                              orient=tk.HORIZONTAL,
                              command=lambda v, u=unit, l=lbl, k=param_key: self._on_pico_ecg_slider_move(k, v, u, l))
            scale.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)
            scale.bind('<ButtonRelease-1>', lambda e, k=param_key: self._pico_ecg_apply_param(k))

            help_txt = ('Heart Rate: frecuencia cardíaca del modelo en latidos '
                        '/ minuto (bpm). Moverlo cambia la cadencia del ECG '
                        'sintetizado.'
                        if param_key == 'hr' else
                        'Amplitude: escala (en %) del ECG/PPG sintetizado '
                        'respecto al rango de salida. Bájalo para señales '
                        'menos intensas o para evitar saturar el front-end.')
            ToolTip(scale, help_txt)
            ToolTip(lbl, help_txt)

    def _build_pace_bench_section(self, parent):
        """Panel for pace_sim: pacemaker test-bench controls. Hidden until the
        pace_sim firmware is detected."""
        self.pace_bench_frame, card = self._make_card(parent, 'Pacemaker Bench')

        # Bench mode on/off
        r1 = tk.Frame(card, bg=C_CARD)
        r1.pack(fill=tk.X, pady=(0, 4))
        tk.Label(r1, text='Bench mode', bg=C_CARD, fg=C_TEXT,
                 font=FB).pack(side=tk.LEFT)
        self.bench_var = tk.BooleanVar(value=False)
        bench_tt_text = ('Activa la bancada de pruebas closed-loop de '
                         'marcapasos. En ON, el simulador genera un ritmo '
                         'sinusal en ausencia de estimulación; los latidos '
                         'del DUT (pulsos paceados) se detectan y capturan. '
                         'Copia este modo on/off del firmware.')
        bench_lbl = tk.Label(r1, text='Bench mode', bg=C_CARD, fg=C_TEXT, font=FB)
        bench_lbl.pack(side=tk.LEFT)
        ToolTip(bench_lbl, bench_tt_text)
        bench_tog = ToggleSwitch(r1, self.bench_var, command=self._on_bench_toggle,
                                 bg=C_CARD)
        bench_tog.pack(side=tk.LEFT, padx=(8, 0))
        ToolTip(bench_tog, bench_tt_text)
        self.bench_status_lbl = tk.Label(r1, text='OFF', bg=C_CARD,
                                         fg=C_TEXT2, font=FS)
        self.bench_status_lbl.pack(side=tk.LEFT, padx=(5, 0))

        # CENELEC pulse polarity
        r2 = tk.Frame(card, bg=C_CARD)
        r2.pack(fill=tk.X, pady=(0, 4))
        cenelecpol_tt = ('Polaridad del pulso CENELEC. pos (por defecto) = '
                         'la deflexión del pulso es positiva; si el DUT '
                         'inyecta el pulso con polaridad negativa, cambia '
                         'este interruptor para que el detector la '
                         'interprete correctamente.')
        cenelecpol_lbl2 = tk.Label(r2, text='CENELEC pol.', bg=C_CARD,
                                   fg=C_TEXT, font=FB)
        cenelecpol_lbl2.pack(side=tk.LEFT)
        ToolTip(cenelecpol_lbl2, cenelecpol_tt)
        self.cenelecpol_var = tk.BooleanVar(value=False)  # False = positive
        cenelecpol_tog = ToggleSwitch(r2, self.cenelecpol_var,
                                      command=self._on_cenelecpol_toggle,
                                      bg=C_CARD)
        cenelecpol_tog.pack(side=tk.LEFT, padx=(8, 0))
        ToolTip(cenelecpol_tog, cenelecpol_tt)
        self.cenelecpol_lbl = tk.Label(r2, text='pos', bg=C_CARD,
                                       fg=C_TEXT2, font=FS)
        self.cenelecpol_lbl.pack(side=tk.LEFT, padx=(5, 0))
        self.cenelecpol_row = r2

        r_model = tk.Frame(card, bg=C_CARD)
        r_model.pack(fill=tk.X, pady=(0, 4))
        tk.Label(r_model, text='Cell model', bg=C_CARD, fg=C_TEXT,
             font=FB).pack(side=tk.LEFT)
        self.cellmodel_var = tk.StringVar(value='Proposed')
        self.cellmodel_combo = ttk.Combobox(
            r_model, textvariable=self.cellmodel_var, state='readonly', width=22,
            values=('Proposed', 'UoA-NL'), font=FS)
        self.cellmodel_combo.pack(side=tk.LEFT, padx=(8, 0))
        self.cellmodel_combo.bind('<<ComboboxSelected>>',
                      self._on_cellmodel_change)
        ToolTip(self.cellmodel_combo,
            'Proposed: modelo semi-lineal de Andalam. UoA-NL: modelo '
            'no lineal con restitución exponencial de Yip et al.')
        self.cellmodel_row = r_model

        # Report / sense buttons
        r3 = tk.Frame(card, bg=C_CARD)
        r3.pack(fill=tk.X, pady=(0, 2))
        _flat_btn(r3, '📋 Report', self._send_pace_report,
                  bg=C_BTN, fg=C_TEXT, font=FB).pack(side=tk.LEFT, padx=(0, 6))
        _flat_btn(r3, '🔎 Sense', self._send_psense,
                  bg=C_BTN, fg=C_TEXT, font=FB).pack(side=tk.LEFT)

        # Live ADC readout (V) — updated every plot refresh.
        r4 = tk.Frame(card, bg=C_CARD)
        r4.pack(fill=tk.X, pady=(0, 2))
        self.adc_readout_lbl = tk.Label(r4, text='A —  V —  S —',
                                        bg=C_CARD, fg=C_TEXT2,
                                        font=(_FF, 9, 'bold'), anchor=tk.W)
        self.adc_readout_lbl.pack(fill=tk.X)

        # Min–max range of each ADC channel over the visible window.
        r5 = tk.Frame(card, bg=C_CARD)
        r5.pack(fill=tk.X, pady=(0, 2))
        self.adc_range_lbl = tk.Label(r5, text='', bg=C_CARD, fg=C_TEXT2,
                                      font=FS, anchor=tk.W)
        self.adc_range_lbl.pack(fill=tk.X)

        self.pace_report_text = tk.Text(card, height=8, state=tk.DISABLED,
                        font=('Consolas', 8), bg='#F8FAFC',
                        fg=C_TEXT, relief='flat', wrap=tk.WORD,
                        highlightthickness=1,
                        highlightbackground=C_BORDER)
        self.pace_report_text.pack(fill=tk.X, pady=(4, 4))

        # ADC y-axis mode for the pace traces.
        r6 = tk.Frame(card, bg=C_CARD)
        r6.pack(fill=tk.X, pady=(0, 2))
        pace_fixed_tt = ('Escala del eje Y de las trazas Pace-Sense ADC. auto '
                         '(por defecto) ajusta el rango a la ventana visible '
                         '(útil porque la señal va montada sobre ~1.9 V de '
                         'DC); 0–3.5 V fija la escala completa para comparar '
                         'con el osciloscopio y ver el nivel DC real de los '
                         'pines.')
        pace_fixed_lbl2 = tk.Label(r6, text='ADC eje 0–3.5 V', bg=C_CARD,
                                   fg=C_TEXT, font=FB)
        pace_fixed_lbl2.pack(side=tk.LEFT)
        ToolTip(pace_fixed_lbl2, pace_fixed_tt)
        self.pace_fixed_var = tk.BooleanVar(value=False)
        pace_fixed_tog = ToggleSwitch(r6, self.pace_fixed_var,
                                      command=self._on_pace_fixed_toggle,
                                      bg=C_CARD)
        pace_fixed_tog.pack(side=tk.LEFT, padx=(8, 0))
        ToolTip(pace_fixed_tog, pace_fixed_tt)
        self.pace_fixed_lbl = tk.Label(r6, text='auto', bg=C_CARD,
                                       fg=C_TEXT2, font=FS)
        self.pace_fixed_lbl.pack(side=tk.LEFT, padx=(5, 0))
        self.pace_fixed_scale = False

        # Pacing demand-only (asystole): no intrinsic rhythm, beats only when
        # the DUT captures (set pacedemand on/off in the firmware).
        r7 = tk.Frame(card, bg=C_CARD)
        r7.pack(fill=tk.X, pady=(0, 4))
        pacedemand_tt = ('Modo a demanda (ásistole): ON suprime el ritmo '
                         'intrínseco del simulador, dejando una línea plana '
                         'salvo los latidos capturados de los pulsos del DUT. '
                         'Sirve para aislar y verificar la detección/captura '
                         'del marcapasos sin ritmo sinusal de fondo. Copia el '
                         'comando pacedemand del firmware.')
        pacedemand_lbl2 = tk.Label(r7, text='Pace-only (ásistole)', bg=C_CARD,
                                   fg=C_TEXT, font=FB)
        pacedemand_lbl2.pack(side=tk.LEFT)
        ToolTip(pacedemand_lbl2, pacedemand_tt)
        self.pacedemand_var = tk.BooleanVar(value=False)
        pacedemand_tog = ToggleSwitch(r7, self.pacedemand_var,
                                      command=self._on_pacedemand_toggle,
                                      bg=C_CARD)
        pacedemand_tog.pack(side=tk.LEFT, padx=(8, 0))
        ToolTip(pacedemand_tog, pacedemand_tt)
        self.pacedemand_lbl = tk.Label(r7, text='OFF', bg=C_CARD,
                                       fg=C_TEXT2, font=FS)
        self.pacedemand_lbl.pack(side=tk.LEFT, padx=(5, 0))

        r8 = tk.Frame(card, bg=C_CARD)
        r8.pack(fill=tk.X, pady=(0, 4))
        tk.Label(r8, text='Pace source', bg=C_CARD, fg=C_TEXT,
             font=FB).pack(side=tk.LEFT)
        self.pacing_mode_var = tk.StringVar(value='A -> V')
        self.pacing_mode_combo = ttk.Combobox(
            r8, textvariable=self.pacing_mode_var, state='readonly', width=22,
            values=('A only', 'A -> V', 'V only', 'A + V'), font=FS)
        self.pacing_mode_combo.pack(side=tk.LEFT, padx=(8, 0))
        self.pacing_mode_combo.bind('<<ComboboxSelected>>',
                        self._on_pacing_mode_change)
        ToolTip(self.pacing_mode_combo,
            'A only: sólo aurícula. A -> V: pulso auricular con conducción '
            'AV según PR. V only: sólo ventrículo. A + V: acepta ambos '
            'canales externos.')

        self.pace_bench_frame.pack_forget()

    def _build_pace_detect_section(self, parent):
        """Detection configuration (threshold/debounce/refractory/IC gain) for
        pace_sim. Hidden unless the pace_sim firmware is detected. Values are
        sent to the firmware as '> set <param> <value>' on slider release."""
        self.pace_detect_outer, card = self._make_card(parent, 'Detección Pace-sense')
        self.pace_slider_widgets = {}

        for param_key, label, unit, from_, to_, default, res in PACE_DETECT_PARAMS:
            row = tk.Frame(card, bg=C_CARD)
            row.pack(fill=tk.X, pady=2)

            tk.Label(row, text=label, bg=C_CARD, fg=C_TEXT2, font=FS,
                     width=14, anchor=tk.W).pack(side=tk.LEFT)

            var = tk.DoubleVar(value=default)
            val_label = tk.Label(row, text=self._fmt_pace_val(default, res, unit),
                                 bg=C_CARD, fg=C_TEXT, font=FBB,
                                 width=12, anchor=tk.E)
            val_label.pack(side=tk.RIGHT)

            scale = ttk.Scale(row, variable=var, from_=from_, to=to_,
                              orient=tk.HORIZONTAL,
                              command=lambda v, k=param_key: self._on_pace_slider(k))
            scale.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 4))
            scale.bind('<ButtonRelease-1>', lambda e, k=param_key: self._apply_pace_param(k))

            self.pace_slider_widgets[param_key] = (var, val_label, (unit, res))

            help_txt = PACE_DETECT_HELP.get(param_key)
            if help_txt:
                # Bind to the actual interactive widgets (slider + value label),
                # NOT the parent frame: in Tk, events over a child widget go to
                # the child, so a frame-bound tooltip would never trigger where
                # the user actually hovers.
                ToolTip(scale, help_txt)
                ToolTip(val_label, help_txt)

        self.pace_detect_outer.pack_forget()

    @staticmethod
    def _fmt_pace_val(val, res, unit):
        return f"{int(round(val)) if res >= 1 else val:.3f} {unit}"

    def _on_pace_slider(self, param_key):
        spec = self.pace_slider_widgets.get(param_key)
        if not spec:
            return
        var, label, (unit, res) = spec
        label.config(text=self._fmt_pace_val(var.get(), res, unit))

    def _apply_pace_param(self, param_key):
        if not self.connected:
            return
        spec = self.pace_slider_widgets.get(param_key)
        if not spec:
            return
        var, label, (unit, res) = spec
        val = var.get()
        if res >= 1:
            self._send_cmd(f"set {param_key} {int(round(val))}")
        else:
            self._send_cmd(f"set {param_key} {val:.3f}")

    def _pace_param_value(self, param_key, default):
        spec = self.pace_slider_widgets.get(param_key)
        if spec:
            try:
                return float(spec[0].get())
            except Exception:
                pass
        return default

    def _on_bench_toggle(self):
        if not self.connected:
            return
        on = self.bench_var.get()
        self._send_cmd("bench " + ("on" if on else "off"))
        self.bench_status_lbl.config(
            text='ON' if on else 'OFF',
            fg='#15803D' if on else C_TEXT2)

    def _on_pacedemand_toggle(self):
        if not self.connected:
            return
        on = self.pacedemand_var.get()
        self._send_cmd("set pacedemand " +
                       (self._pacing_mode_command() if on else "off"))
        self.pacedemand_lbl.config(
            text='ON' if on else 'OFF',
            fg='#15803D' if on else C_TEXT2)

    def _pacing_mode_command(self):
        return {
            'A only': 'atrial',
            'A -> V': 'atrial_v',
            'V only': 'ventricular',
            'A + V': 'both',
        }.get(self.pacing_mode_var.get(), 'atrial_v')

    def _on_pacing_mode_change(self, event=None):
        if self.connected and self.pacedemand_var.get():
            self._send_cmd('set pacedemand ' + self._pacing_mode_command())

    def _on_cenelecpol_toggle(self):
        if not self.connected:
            return
        neg = self.cenelecpol_var.get()
        self._send_cmd(f"set cenelecpol {'neg' if neg else 'pos'}")
        self.cenelecpol_lbl.config(
            text='neg' if neg else 'pos',
            fg='#15803D' if neg else C_TEXT2)

    def _on_cellmodel_change(self, event=None):
        if not self.connected:
            return
        model = 'uoa_nl' if self.cellmodel_var.get() == 'UoA-NL' else 'proposed'
        self._send_cmd(f'set cellmodel {model}')

    def _on_pace_fixed_toggle(self):
        self.pace_fixed_scale = bool(self.pace_fixed_var.get())
        self.pace_fixed_lbl.config(
            text='fijo' if self.pace_fixed_scale else 'auto',
            fg='#15803D' if self.pace_fixed_scale else C_TEXT2)
        self._plot_dirty = True

    def _send_pace_report(self):
        if self.connected:
            self._request_pace_text_command("report")

    def _send_psense(self):
        if self.connected:
            self._request_pace_text_command("psense")

    def _request_pace_text_command(self, command):
        """Show a text-only pace report without losing binary stream parsing."""
        self._pace_command_capture = True
        self._pace_command_stream_was_on = self.streaming
        self.pace_report_text.config(state=tk.NORMAL)
        self.pace_report_text.delete('1.0', tk.END)
        self.pace_report_text.insert(tk.END, f"> {command}\n")
        self.pace_report_text.config(state=tk.DISABLED)

        if self.streaming:
            self._send_cmd("stream off")
            self.streaming = False
            self.stream_btn.config(text="Stream ON")
        self._send_cmd(command)
        self.root.after(700, self._finish_pace_text_command)

    def _finish_pace_text_command(self):
        if not self.connected:
            self._pace_command_capture = False
            return
        if self._pace_command_stream_was_on:
            self._send_cmd("stream on")
            self.streaming = True
            self.stream_btn.config(text="Stream OFF")
        self._pace_command_capture = False

    def _build_device_section(self, parent):
        outer, card = self._make_card(parent, 'Device Controls')
        outer.pack(fill=tk.X, padx=6, pady=3)

        def _row(label):
            r = tk.Frame(card, bg=C_CARD)
            r.pack(fill=tk.X, pady=3)
            tk.Label(r, text=label, bg=C_CARD, fg=C_TEXT2, font=FS,
                     width=11, anchor=tk.W).pack(side=tk.LEFT)
            return r

        # Signal type
        r = _row('Signal type')
        self.sig_type_var = tk.StringVar(value='ecg')
        sig_cb = ttk.Combobox(r, textvariable=self.sig_type_var,
                               values=['ecg', 'sine', 'tri', 'sq', 'pulse', 'cenelec'],
                               state='readonly', font=FB, width=10)
        sig_cb.pack(side=tk.LEFT, padx=(4, 0))
        sig_cb.bind('<<ComboboxSelected>>', self._on_sig_type_change)
        ToolTip(sig_cb, 'Forma de onda de la señal sintetizada en la salida: '
                        'ecg (trazado cardíaco), sine (senoidal), tri '
                        '(triangular), sq (cuadrada), pulse (pulso) o cenelec '
                        '(pulso de prueba CENELEC para marcapasos).')

        # Buzzer toggle
        r = _row('Buzzer')
        buzzer_tt = 'Activa/desactiva el zumbador (buzzer) de la placa. Habilita una notificación audible al detectar eventos.'
        self.buzzer_var = tk.BooleanVar(value=False)
        buzzer_tog = ToggleSwitch(r, self.buzzer_var,
                                  command=self._on_buzzer_toggle, bg=C_CARD)
        buzzer_tog.pack(side=tk.LEFT, padx=(4, 0))
        ToolTip(buzzer_tog, buzzer_tt)
        try:
            ToolTip(r.winfo_children()[0], buzzer_tt)
        except Exception:
            pass

        # LED toggle
        r = _row('LED')
        led_tt = 'Activa/desactiva el LED indicador de la placa (parpadea al ritmo del latido).'
        self.led_var = tk.BooleanVar(value=True)
        led_tog = ToggleSwitch(r, self.led_var,
                               command=self._on_led_toggle, bg=C_CARD)
        led_tog.pack(side=tk.LEFT, padx=(4, 0))
        ToolTip(led_tog, led_tt)
        try:
            ToolTip(r.winfo_children()[0], led_tt)
        except Exception:
            pass

        # HRV toggle
        r = _row('HRV')
        hrv_tt = ('Variabilidad de la frecuencia cardíaca: ON añade una '
                  'ligera irregularidad en los intervalos R-R simulada '
                  '(más fisiológico). OFF = ritmo perfectamente regular.')
        self.hrv_var = tk.BooleanVar(value=False)
        hrv_tog = ToggleSwitch(r, self.hrv_var,
                               command=self._on_hrv_toggle, bg=C_CARD)
        hrv_tog.pack(side=tk.LEFT, padx=(4, 0))
        ToolTip(hrv_tog, hrv_tt)
        try:
            ToolTip(r.winfo_children()[0], hrv_tt)
        except Exception:
            pass

        # Offset slider
        r = _row('Offset')
        offset_tt = ('Offset DC aplicado a la señal de salida (cuentas crudas '
                     'del DAC). Desplaza la línea base: positivo la sube, '
                     'negativo la baja. Útil para compensar el nivel DC del '
                     'front-end antes del ADC.')
        self.offset_var = tk.IntVar(value=0)
        self.offset_label = tk.Label(r, text='0', bg=C_CARD, fg=C_TEXT,
                                      font=FBB, width=7, anchor=tk.E)
        self.offset_label.pack(side=tk.RIGHT)
        _flat_btn(r, 'Set', self._apply_offset, bg=C_BTN, font=FS).pack(
            side=tk.RIGHT, padx=(3, 0))
        offset_scale = ttk.Scale(r, variable=self.offset_var, from_=-16384,
                                 to=16384, orient=tk.HORIZONTAL,
                                 command=self._on_offset_change)
        offset_scale.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 4))
        ToolTip(offset_scale, offset_tt)
        try:
            ToolTip(r.winfo_children()[0], offset_tt)
        except Exception:
            pass

        # Cal Gain slider
        r = _row('Cal Gain')
        calgain_tt = ('Ganancia de calibración (x) aplicada a la señal de '
                      'salida. Ajusta la amplitud global frente al front-end; '
                      'súbela para compensar atenuación, bájala para evitar '
                      'saturación.')
        self.cal_gain_var = tk.DoubleVar(value=1.0)
        self.cal_gain_label = tk.Label(r, text='1.00', bg=C_CARD, fg=C_TEXT,
                                        font=FBB, width=7, anchor=tk.E)
        self.cal_gain_label.pack(side=tk.RIGHT)
        _flat_btn(r, 'Set', self._apply_cal_gain, bg=C_BTN, font=FS).pack(
            side=tk.RIGHT, padx=(3, 0))
        calgain_scale = ttk.Scale(r, variable=self.cal_gain_var, from_=0.05,
                                  to=10.0, orient=tk.HORIZONTAL,
                                  command=self._on_cal_change)
        calgain_scale.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 4))
        ToolTip(calgain_scale, calgain_tt)
        try:
            ToolTip(r.winfo_children()[0], calgain_tt)
        except Exception:
            pass

    # ------------------------------------------------------------------
    # pico_ecg helpers
    # ------------------------------------------------------------------

    def _setup_pico_ecg_ui(self):
        """Show the pico_ecg panel and hide model/rhythm panels."""
        self.root.title("ECG GUI - pico_ecg (Stored Signals)")
        self.sample_rate = 500
        self.ppg_index = None
        self.pace_mode = False
        self.pace_indexes = None
        if self.serial_thread:
            self.serial_thread.num_channels = 9
        try:
            self.pace_bench_frame.pack_forget()
        except Exception:
            pass

        # Hide model-based panels (must forget the OUTER/card frame, not the content frame)
        self.params_outer.pack_forget()
        self.rhythm_frame.pack_forget()

        # Re-pack pico_ecg panel (after display section)
        self.pico_ecg_frame.pack(fill=tk.X, padx=5, pady=(3, 3))

        # Request the signal list from the device, then auto-start streaming
        self.root.after(400, self._pico_ecg_request_list)
        self.root.after(2200, self._pico_ecg_auto_stream)

    def _pico_ecg_auto_stream(self):
        """Start streaming automatically after device detection."""
        if self.connected and not self.streaming:
            self._send_cmd("stream on")
            self.streaming = True
            self.pico_ecg_stream_btn.config(text="Stream OFF")
            self.stream_btn.config(text="Stream OFF")
            self.status_label.config(text="Connected (streaming)", foreground="blue")

    def _pico_ecg_toggle_stream(self):
        """Toggle stream on/off from the pico_ecg panel button."""
        if not self.connected:
            return
        if self.streaming:
            self._send_cmd("stream off")
            self.streaming = False
            self.pico_ecg_stream_btn.config(text="Stream ON")
            self.stream_btn.config(text="Stream ON")
            self.status_label.config(text="Connected (menu)", foreground="green")
        else:
            self._send_cmd("stream on")
            self.streaming = True
            self.pico_ecg_stream_btn.config(text="Stream OFF")
            self.stream_btn.config(text="Stream OFF")
            self.status_label.config(text="Connected (streaming)", foreground="blue")

    def _pico_ecg_request_list(self):
        """Send 'list' to the device and start collecting the response."""
        if not self.connected:
            return
        self._pico_ecg_list_lines = []
        self._pico_ecg_collecting = True
        self.pico_ecg_status_lbl.config(text="Fetching...", foreground="orange")
        self._send_cmd("list")
        # Give the device enough time to respond, then parse
        self.root.after(1200, self._pico_ecg_parse_list)

    def _pico_ecg_parse_list(self):
        """Parse accumulated 'list' output and populate the Combobox."""
        self._pico_ecg_collecting = False
        import re
        files = []
        # Expected line format:  "  [ 0] Sinus_rhythm_N_11       2048x1"
        # or with varying spaces: "  [15] Atrial_fibrillation_N_38  2048x1"
        pattern = re.compile(r'\[\s*(\d+)\]\s+(\S+)')
        for line in self._pico_ecg_list_lines:
            m = pattern.search(line)
            if m:
                idx = int(m.group(1))
                name = m.group(2)
                # Replace underscores and make it human-readable
                display = name.replace('_', ' ')
                files.append((idx, name, display))
        self._pico_ecg_files = files
        if files:
            labels = [f"{f[0]:2d}: {f[2]}" for f in files]
            self.pico_ecg_file_combo['values'] = labels
            self.pico_ecg_file_combo.current(0)
            self.pico_ecg_status_lbl.config(
                text=f"{len(files)} signals found", foreground="green"
            )
        else:
            self.pico_ecg_file_combo['values'] = []
            self.pico_ecg_status_lbl.config(text="No signals found", foreground="red")

    def _on_pico_ecg_file_change(self, event=None):
        """Send 'set file N' when the user selects a signal."""
        if not self.connected or not self._pico_ecg_files:
            return
        idx = self.pico_ecg_file_combo.current()
        if 0 <= idx < len(self._pico_ecg_files):
            file_idx = self._pico_ecg_files[idx][0]
            self._send_cmd(f"set file {file_idx}")

    def _on_pico_ecg_slider_move(self, param_key, val, unit, label):
        label.config(text=f'{int(float(val))} {unit}')
        self._pico_ecg_apply_param(param_key)

    def _pico_ecg_apply_param(self, param_key):
        """Apply a slider value for hr or amp."""
        if not self.connected:
            return
        val = self.slider_vars.get(param_key)
        if val is None:
            return
        self._send_cmd(f"set {param_key} {int(val.get())}")

    def _on_sig_type_change(self, event=None):
        if self.connected:
            self._send_cmd(f"set type {self.sig_type_var.get()}")

    def _on_buzzer_toggle(self):
        if self.connected:
            self._send_cmd(f"set buzzer {'on' if self.buzzer_var.get() else 'off'}")

    def _on_led_toggle(self):
        if self.connected:
            self._send_cmd(f"set led {'on' if self.led_var.get() else 'off'}")

    def _on_offset_change(self, val):
        self.offset_label.config(text=str(int(float(val))))
        self._apply_offset()

    def _apply_offset(self):
        if self.connected:
            self._send_cmd(f"set offset {int(self.offset_var.get())}")

    def _on_hrv_toggle(self):
        if self.connected:
            self._send_cmd(f"set hrv {'on' if self.hrv_var.get() else 'off'}")

    def _on_cal_change(self, val):
        self.cal_gain_label.config(text=f"{float(val):.2f}")

    def _apply_cal_gain(self):
        if self.connected:
            self._send_cmd(f"set cal {float(self.cal_gain_var.get()):.3f}")

    def _build_leads_section(self, parent):
        outer, card = self._make_card(parent, 'Visible Leads')
        outer.pack(fill=tk.X, padx=6, pady=(3, 8))
        self.leads_outer = outer
        self.leads_card = card
        self._populate_leads()

    def _populate_leads(self):
        """(Re)build the lead checkboxes for the current project's lead count."""
        for w in self.leads_card.winfo_children():
            w.destroy()
        self.visible_leads = [True] * self.num_leads
        self.lead_vars = []
        cols = 3
        for i, name in enumerate(self.lead_names[:self.num_leads]):
            var = tk.BooleanVar(value=True)
            self.lead_vars.append(var)
            cb = ttk.Checkbutton(self.leads_card, text=name, variable=var,
                                 command=self._on_lead_toggle)
            cb.grid(row=i // cols, column=i % cols,
                    sticky=tk.W, padx=(0, 10), pady=2)
        for c in range(cols):
            self.leads_card.columnconfigure(c, weight=1)

    def _build_plot(self, parent):
        # --- Plot toolbar (Record + Save PNG) ---
        ptb = tk.Frame(parent, bg=C_CARD)
        ptb.pack(fill=tk.X, side=tk.BOTTOM)
        tk.Frame(ptb, bg=C_BORDER, height=1).pack(fill=tk.X)
        btns = tk.Frame(ptb, bg=C_CARD)
        btns.pack(fill=tk.X, padx=8, pady=4)
        self._rec_btn = _flat_btn(btns, '\u23fa  Record', self._toggle_recording,
                                   bg=C_BTN, fg=C_TEXT, font=FB)
        self._rec_btn.pack(side=tk.LEFT, padx=(0, 6))
        _flat_btn(btns, '\U0001f4f7  Save PNG', self._save_snapshot,
                  bg=C_BTN, fg=C_TEXT, font=FB).pack(side=tk.LEFT)
        self._rec_status = tk.Label(btns, text='', bg=C_CARD,
                                    fg='#DC2626', font=FBB)
        self._rec_status.pack(side=tk.LEFT, padx=(10, 0))

        # Modern plot: white background, subtle grid, clean typography
        matplotlib.rcParams.update({
            'font.family':    'DejaVu Sans',
            'axes.facecolor': '#FFFFFF',
            'figure.facecolor': '#FFFFFF',
            'axes.edgecolor': C_BORDER,
            'axes.spines.top':    False,
            'axes.spines.right':  False,
            'axes.grid':      True,
            'grid.color':     '#E2E8F0',
            'grid.linewidth': 0.8,
            'axes.labelcolor': C_TEXT2,
            'xtick.color':    C_TEXT2,
            'ytick.color':    C_TEXT2,
        })

        self.fig, (self.ax, self.ax_pace) = plt.subplots(
            2, 1, figsize=(10, 7), dpi=100, sharex=True,
            gridspec_kw={'height_ratios': [3, 2]})
        self.fig.set_facecolor('#FFFFFF')
        self.fig.subplots_adjust(left=0.07, right=0.97, top=0.93, bottom=0.07, hspace=0.35)

        self.ax.set_title('ECG Waveform', fontsize=13, fontweight='bold',
                           color=C_TEXT, pad=10)
        self.ax.set_ylabel('Amplitude (mV)', fontsize=9, color=C_TEXT2)
        self.ax.set_xlim(0, self.display_seconds)
        self.ax.set_ylim(-self.amplitude_scale, self.amplitude_scale)

        # HR annotation in upper-left of ECG axes
        self._hr_text = self.ax.text(
            0.01, 0.97, '', transform=self.ax.transAxes,
            fontsize=14, fontweight='bold', color=C_ACCENT,
            va='top', ha='left', alpha=0.9)

        self.lines = []
        for i in range(9):
            line, = self.ax.plot([], [], color=LEAD_COLORS[i],
                                  linewidth=1.0, label=LEAD_NAMES[i], alpha=0.9)
            self.lines.append(line)

        handles, labels = self.ax.get_legend_handles_labels()
        self.ax_legend = self.ax.legend(handles[:self.num_leads], labels[:self.num_leads],
                                        loc='upper right', fontsize=7, ncol=3,
                                        framealpha=0.9, edgecolor=C_BORDER)

        # Pace-sense ADC channels (A/V/S) — shown only in pace_sim mode.
        self.ax_pace.set_title('Pace-Sense ADC (V)', fontsize=10,
                               fontweight='bold', color=C_TEXT)
        self.ax_pace.set_ylabel('V', fontsize=9, color=C_TEXT2)
        self.ax_pace.set_xlim(0, self.display_seconds)
        self.ax_pace.set_ylim(0, 3.5)
        self.pace_line_colors = ['#d81b60', '#1565c0', '#6a1b9a']
        self.pace_line_names = ['Atrial', 'Ventric.', 'Surface']
        self.pace_lines = []
        for name, colr in zip(self.pace_line_names, self.pace_line_colors):
            line, = self.ax_pace.plot([], [], color=colr, linewidth=1.0,
                                      label=name, alpha=0.9)
            self.pace_lines.append(line)
        self.pace_threshold_lines = []
        threshold_styles = [
            ('A thresh', '#b91c1c', '--'),
            ('A cross', '#f97316', ':'),
            ('V thresh', '#1d4ed8', '--'),
            ('V cross', '#0ea5e9', ':'),
        ]
        for label, color, style in threshold_styles:
            line, = self.ax_pace.plot([], [], color=color, linestyle=style,
                                      linewidth=0.9, label=label, alpha=0.9)
            self.pace_threshold_lines.append(line)
        self.ax_pace.legend(loc='upper right', fontsize=6, ncol=4,
                            framealpha=0.9, edgecolor=C_BORDER)
        self.ax_pace.set_visible(False)

        self.canvas = FigureCanvasTkAgg(self.fig, master=parent)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        toolbar = NavigationToolbar2Tk(self.canvas, parent)
        toolbar.update()

    def _refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [f"{p.device} - {p.description}" for p in ports]
        self.port_combo['values'] = port_list
        if port_list:
            self.port_combo.current(0)
            self._port_map = {f"{p.device} - {p.description}": p.device for p in ports}
        else:
            self._port_map = {}
            self.port_combo.set("")

    def _toggle_connect(self):
        if self.connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        sel = self.port_var.get()
        if not sel or sel not in self._port_map:
            messagebox.showwarning("No Port", "Select a serial port first.")
            return

        # If a previous thread is still alive (e.g. after a disconnect race),
        # retire it before starting a new session.
        if self.serial_thread and self.serial_thread.is_alive():
            self.serial_thread.stop()
            self.serial_thread.join(0.5)
            self.serial_thread = None

        port = self._port_map[sel]
        # Drop any stale commands/plot data left over from a previous session.
        with self.queue_lock:
            self.data_queue.clear()
        self._ring_n = 0
        self._plotted_len = 0
        while not self.cmd_queue.empty():
            try:
                self.cmd_queue.get_nowait()
            except queue.Empty:
                break
        self.serial_thread = SerialThread(
            port, self.data_queue, self.cmd_queue,
            self._on_status, self._on_info_line,
            self._on_pkt, self.queue_lock
        )
        # Project may already be known from a previous session; the new thread
        # must know the frame layout immediately or binary frames will be
        # parsed with 9 channels instead of 10 and dropped.
        if self.project_type in PROJECTS:
            self.serial_thread.num_channels = self._num_channels_for(self.project_type)
            proj = PROJECTS[self.project_type]
            self.serial_thread.adc_indexes = proj.get("adc_channels", ())
            self.serial_thread.mv_max = proj.get("mv_max", 50.0)
        self.serial_thread.start()
        self.connect_btn.config(text="Connecting...")

    def _disconnect(self):
        if self.serial_thread:
            self.serial_thread.stop()
            self.serial_thread.join(0.5)
            self.serial_thread = None
        with self.queue_lock:
            self.data_queue.clear()
        self._ring_n = 0
        while not self.cmd_queue.empty():
            try:
                self.cmd_queue.get_nowait()
            except queue.Empty:
                break
        self.connected = False
        self.streaming = False
        self.connect_btn.config(text="Connect")
        self.status_label.config(text="Disconnected", foreground="gray")
        self._plot_dirty = True
        self._plotted_len = 0

    def _shutdown(self):
        """Stop background resources before destroying the Tk application."""
        if getattr(self, '_shutdown_started', False):
            return
        self._shutdown_started = True
        thread = self.serial_thread
        self.serial_thread = None
        if thread is not None:
            thread.stop()
            thread.join(2.0)
        try:
            self.root.destroy()
        except tk.TclError:
            pass

    def _on_status(self, status):
        def _update():
            if status == "connected":
                self.connected = True
                self.connect_btn.config(text="Disconnect")
                self.stream_btn.config(state=tk.NORMAL)
                self.status_label.config(text="Connected", fg=C_LED_OK)
                self._status_led.itemconfig(self._led_oval, fill=C_LED_OK)
                self._plot_dirty = True
                # Update statusbar port label
                sel = self.port_var.get()
                self._sb_port.config(text=sel.split(' - ')[0] if sel else 'Device')
                self._detect_project()
                self.root.after(4000, self._auto_stream)
            elif status == "disconnected":
                self.connected = False
                self.streaming = False
                self.connect_btn.config(text="Connect")
                self.stream_btn.config(state=tk.DISABLED, text="Stream ON")
                self.status_label.config(text="Disconnected", fg=C_TEXT2)
                self._status_led.itemconfig(self._led_oval, fill=C_LED_NO)
                self._sb_port.config(text='No device')
                self._plot_dirty = True
            elif status.startswith("error"):
                self.status_label.config(text=status, fg='#DC2626')
                self._status_led.itemconfig(self._led_oval, fill='#EF4444')
        self.root.after(0, _update)

    def _auto_stream(self):
        if self.connected and not self.streaming:
            self._send_cmd("stream on")
            self.streaming = True
            self.stream_btn.config(text="Stream OFF")
            self.status_label.config(text="Connected (streaming)", foreground="blue")

    def _on_info_line(self, line):
        def _update():
            if "\033[" in line or "[2J" in line:
                return
            if "--- TERMINAL MENU" in line:
                return
            if "CLI ready" in line:
                return

            # ---- pico_ecg: accumulate 'list' output ----
            if self._pico_ecg_collecting:
                self._pico_ecg_list_lines.append(line)

            # ---- pico_ecg: detect 'ver' response ----
            if self.project_type is None and ("pico_ecg" in line.lower() or "ecg monitor" in line.lower()):
                self.project_type = PICO_ECG_KEY
                self._setup_pico_ecg_ui()

            # Parse 'get' response to sync sliders
            if "=" in line:
                self._parse_get_line(line)

            if "=" in line:
                parts = line.split("=", 1)
                if len(parts) == 2 and parts[0].isupper():
                    self.project_info[parts[0]] = parts[1]

            self.info_text.config(state=tk.NORMAL)
            self.info_text.insert(tk.END, line + "\n")
            self.info_text.see(tk.END)
            self.info_text.config(state=tk.DISABLED)
            if self._pace_command_capture:
                self.pace_report_text.config(state=tk.NORMAL)
                self.pace_report_text.insert(tk.END, line + "\n")
                self.pace_report_text.see(tk.END)
                self.pace_report_text.config(state=tk.DISABLED)
            self._terminal_append(('txt', line))

            if "PROJECT" in self.project_info:
                proj = self.project_info["PROJECT"]
                if proj in PROJECTS and self.project_type != proj:
                    self.project_type = proj
                    self._setup_project_ui(proj)
        self.root.after(0, _update)

    def _detect_project(self):
        self.project_info.clear()
        self.project_type = None
        # Reset pico_ecg list state on each new connection
        self._pico_ecg_list_lines = []
        self._pico_ecg_collecting = False
        self._pico_ecg_files = []
        # Hide all control panels until project is detected
        try:
            self.pico_ecg_frame.pack_forget()
            self.params_outer.pack_forget()
            self.rhythm_frame.pack_forget()
            self.pace_bench_frame.pack_forget()
        except Exception:
            pass
        # Immediately request version and info upon connection
        self._send_cmd("ver")
        self._send_cmd("info")

    def _fallback_detect(self):
        if "PROJECT" not in self.project_info and self.project_type is None and self.connected:
            self._send_cmd("ver")

    def _send_cmd(self, cmd):
        if not self.connected:
            return  # ignore commands from stale callbacks / dead sessions
        self.cmd_queue.put("> " + cmd)

    def _send_play(self):
        self._send_cmd("play")

    def _send_stop(self):
        self._send_cmd("stop")

    def _toggle_stream(self):
        if not self.connected:
            return
        if self.streaming:
            self._send_cmd("stream off")
            self.streaming = False
            self.stream_btn.config(text="Stream ON")
            self.status_label.config(text="Connected (menu)", foreground="green")
        else:
            self._send_cmd("stream on")
            self.streaming = True
            self.stream_btn.config(text="Stream OFF")
            self.status_label.config(text="Connected (streaming)", foreground="blue")

    # ------------------------------------------------------------------
    # Terminal monitor: raw stream bytes + parsed frames + console text
    # ------------------------------------------------------------------

    def _on_pkt(self, raw, values):
        # Called from the serial thread: only bridge into the queue, the
        # UI side drains it on the Tk main loop via _drain_terminal_queue().
        self.terminal_queue.put((raw, values))

    def _drain_terminal_queue(self):
        q = self.terminal_queue
        while True:
            try:
                raw, values = q.get_nowait()
            except queue.Empty:
                break
            self._term_seq += 1
            self._terminal_append(('raw', self._term_seq, raw))
            self._terminal_append(('dec', self._term_seq, list(values)))

    def _open_terminal(self):
        if self.terminal_win is not None:
            try:
                if self.terminal_win.winfo_exists():
                    self.terminal_win.deiconify()
                    self.terminal_win.lift()
                    self._terminal_refresh_view()
                    return
            except Exception:
                pass
            self.terminal_win = None

        win = tk.Toplevel(self.root)
        win.title("Terminal · Datos recibidos y binarios parseados")
        win.geometry("1080x640")
        win.configure(bg=C_BG)
        self.terminal_win = win
        self.terminal_paused = False

        bar = tk.Frame(win, bg=C_BG)
        bar.pack(fill=tk.X, padx=8, pady=(8, 4))

        self.term_pause_btn = _flat_btn(bar, "⏸ Pausa", self._terminal_pause_toggle)
        self.term_pause_btn.pack(side=tk.LEFT, padx=(0, 4))
        _flat_btn(bar, "🗑 Limpiar", self._terminal_clear_all).pack(side=tk.LEFT, padx=4)

        self.term_hex_var = tk.BooleanVar(value=self.term_full_hex)
        _chk = tk.Checkbutton(bar, text="Hex completo", variable=self.term_hex_var,
                              command=self._terminal_on_hex_toggle,
                              bg=C_BG, fg=C_TEXT, font=FB, selectcolor=C_BG,
                              activebackground=C_BG, activeforeground=C_TEXT)
        _chk.pack(side=tk.LEFT, padx=8)

        self.term_scroll_var = tk.BooleanVar(value=self.term_autoscroll)
        _chk2 = tk.Checkbutton(bar, text="Auto-scroll", variable=self.term_scroll_var,
                               command=self._terminal_on_scroll_toggle,
                               bg=C_BG, fg=C_TEXT, font=FB, selectcolor=C_BG,
                               activebackground=C_BG, activeforeground=C_TEXT)
        _chk2.pack(side=tk.LEFT, padx=4)

        _flat_btn(bar, "Stream OFF" if self.streaming else "Stream ON",
                  self._toggle_stream).pack(side=tk.RIGHT, padx=(4, 0))

        _cmd = tk.Entry(bar, font=('Consolas', 9), width=26,
                        bg=C_CARD, fg=C_TEXT, insertbackground=C_TEXT,
                        highlightbackground=C_BORDER, highlightthickness=1, relief='flat')
        _cmd.pack(side=tk.RIGHT, padx=2)
        _cmd.bind('<Return>', lambda e: self._terminal_send_cmd(_cmd))
        _flat_btn(bar, "Enviar", lambda: self._terminal_send_cmd(_cmd)).pack(side=tk.RIGHT, padx=(0, 6))

        body = tk.Frame(win, bg=C_BG)
        body.pack(fill=tk.BOTH, expand=True, padx=8, pady=(0, 8))
        txt = tk.Text(body, wrap=tk.NONE, font=('Consolas', 8),
                      bg='#0B1220', fg='#E2E8F0', insertbackground='#E2E8F0',
                      relief='flat', borderwidth=0, padx=6, pady=6)
        sb = tk.Scrollbar(body, orient=tk.VERTICAL, command=txt.yview)
        txt.config(yscrollcommand=sb.set)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        txt.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        txt.tag_configure('raw', foreground='#7DD3FC')
        txt.tag_configure('dec', foreground='#86EFAC')
        txt.tag_configure('txt', foreground='#FDE68A')
        txt.config(state=tk.DISABLED)
        self.terminal_text = txt

        win.protocol("WM_DELETE_WINDOW", lambda: self._close_terminal(win))
        self._terminal_refresh_view()

    def _close_terminal(self, win):
        self.terminal_text = None
        if self.terminal_win is win:
            self.terminal_win = None
        try:
            win.destroy()
        except Exception:
            pass

    def _terminal_clear_all(self):
        self.terminal_buffer.clear()
        self._terminal_refresh_view()

    def _terminal_pause_toggle(self):
        self.terminal_paused = not self.terminal_paused
        btn = getattr(self, 'term_pause_btn', None)
        if btn is not None:
            try:
                btn.config(text="▶ Reanudar" if self.terminal_paused else "⏸ Pausa")
            except Exception:
                pass
        if not self.terminal_paused:
            self._terminal_refresh_view()

    def _terminal_on_hex_toggle(self):
        self.term_full_hex = bool(self.term_hex_var.get())
        self._terminal_refresh_view()

    def _terminal_on_scroll_toggle(self):
        self.term_autoscroll = bool(self.term_scroll_var.get())
        self._terminal_refresh_view()

    def _terminal_send_cmd(self, ent):
        cmd = ent.get().strip()
        if not cmd:
            return
        ent.delete(0, tk.END)
        self._terminal_append(('txt', f"> {cmd}"))
        if self.streaming:
            self._terminal_append(('txt', "# stream activo: la respuesta se ve mejor con 'Stream OFF'"))
        self._send_cmd(cmd)

    def _terminal_append(self, entry):
        self.terminal_buffer.append(entry)
        if (self.terminal_win is not None and self.terminal_text is not None
                and not self.terminal_paused):
            self._terminal_insert(entry)

    def _terminal_insert(self, entry):
        txt = self.terminal_text
        if txt is None:
            return
        try:
            line, tag = self._term_line(entry)
            txt.config(state=tk.NORMAL)
            txt.insert(tk.END, line + "\n", tag)
            # Keep the widget from growing without bound.
            if int(txt.index('end-1c').split('.')[0]) > 3000:
                txt.delete('1.0', '1000.0')
            if self.term_autoscroll:
                txt.see(tk.END)
            txt.config(state=tk.DISABLED)
        except Exception:
            pass

    def _terminal_refresh_view(self):
        if self.terminal_text is None or self.terminal_win is None:
            return
        try:
            if not self.terminal_win.winfo_exists():
                return
            txt = self.terminal_text
            txt.config(state=tk.NORMAL)
            txt.delete('1.0', tk.END)
            for entry in self.terminal_buffer:
                line, tag = self._term_line(entry)
                txt.insert(tk.END, line + "\n", tag)
            if self.term_autoscroll:
                txt.see(tk.END)
            txt.config(state=tk.DISABLED)
        except Exception:
            pass

    def _term_line(self, entry):
        """Render one buffered entry to (text_line, tag)."""
        kind = entry[0]
        if kind == 'raw':
            _, seq, raw = entry
            if seq > 0:
                hdr = f"#{seq:07d} RAW ({len(raw)}B) "
            else:
                hdr = f"RAW ({len(raw)}B) "
            if len(raw) >= 4 and raw[:2] == b'\xaa\x55' and raw[-2:] == b'\x55\xaa':
                dlen = len(raw) - 4
                if self.term_full_hex:
                    hexs = ' '.join(f"{b:02x}" for b in raw)
                elif len(raw) > 14:
                    hexs = ('AA 55 ' +
                            ' '.join(f"{b:02x}" for b in raw[2:6]) +
                            f" \u2026({dlen}B)\u2026 " +
                            ' '.join(f"{b:02x}" for b in raw[-4:]))
                else:
                    hexs = ' '.join(f"{b:02x}" for b in raw)
            else:
                hexs = ' '.join(f"{b:02x}" for b in raw)
            return hdr + hexs, 'raw'
        if kind == 'dec':
            _, seq, values = entry
            # Pace-sense channels are the trailing ADC slots of the active
            # project's frame (indices 4-6 for both bench projects).
            labels = {}
            pace = getattr(self, 'pace_indexes', None) or ()
            for j, idx in enumerate(pace):
                labels[idx] = ['A', 'V', 'S'][j]
            parts = []
            for k, v in enumerate(values):
                if k in labels:
                    parts.append(f"{labels[k]}({k})={v:+.3f}")
                else:
                    parts.append(f"[{k}]={v:+.3f}")
            pref = f"#{seq:07d} DEC " if seq > 0 else "DEC "
            return pref + " ".join(parts), 'dec'
        return entry[1], 'txt'

    @staticmethod
    def _num_channels_for(proj_key):
        if proj_key in (PACE_SIM_KEY, PACE_SIM_IC_KEY, PACE_SIM_9_KEY):
            return 7
        return 10 if proj_key == "pico_ecg_gaussian_ppg" else 9

    def _setup_project_ui(self, proj_key):
        proj = PROJECTS[proj_key]
        self.root.title(f"ECG GUI - {proj['name']} ({proj['paper']})")
        self.sample_rate = 500 if proj_key == PICO_ECG_KEY else 2000
        self._plot_dirty = True
        self._ring_n = 0   # channel layout changes with the project

        # Hide pico_ecg stored-signal panel when showing a mathematical model project
        try:
            self.pico_ecg_frame.pack_forget()
        except Exception:
            pass

        # pace_sim / pace_sim_intracardiac: enable the pacemaker bench panel
        # and the pace-sense ADC traces (3 channels at the end of the frame).
        is_pace = proj_key in (PACE_SIM_KEY, PACE_SIM_IC_KEY, PACE_SIM_9_KEY)
        is_intracardiac = proj_key in (PACE_SIM_IC_KEY, PACE_SIM_9_KEY)
        self.pace_mode = is_pace
        # Both bench firmwares stream the 3 pace-sense ADC voltages in the
        # last slots (4-6) of the 7-channel frame.
        self.pace_indexes = PACE_ADC_CHANNELS if is_pace else None
        try:
            if is_pace:
                self.pace_bench_frame.pack(fill=tk.X, padx=6, pady=3)
            else:
                self.pace_bench_frame.pack_forget()
        except Exception:
            pass
        try:
            if self.cenelecpol_row is not None:
                if proj_key == PACE_SIM_KEY:
                    self.cenelecpol_row.pack(fill=tk.X, pady=(0, 4))
                else:
                    self.cenelecpol_row.pack_forget()
            if self.cellmodel_row is not None:
                if is_intracardiac:
                    self.cellmodel_row.pack(fill=tk.X, pady=(0, 4))
                else:
                    self.cellmodel_row.pack_forget()
        except Exception:
            pass
        try:
            if is_pace:
                self.pace_detect_outer.pack(fill=tk.X, padx=6, pady=3)
            else:
                self.pace_detect_outer.pack_forget()
        except Exception:
            pass

        # The Gaussian ECG+PPG / pace_sim projects stream a PPG channel. Index 3 for
        # 7-channel pace_sim frames (RA/LA/LL + PPG + 3 ADC); index 9 for
        # 10-channel gaussian frames (9 ECG + PPG). pace_sim_intracardiac has
        # no PPG: its 4 channels are cell-model EGMs only.
        self.ppg_index = 3 if proj_key == PACE_SIM_KEY else (
            9 if proj_key == "pico_ecg_gaussian_ppg" else None)
        self.num_leads = proj.get("num_leads", 9)
        self.lead_names = list(proj.get("leads", LEAD_NAMES))
        # Re-label the plot lines with the active project's channel names so
        # the legend shows e.g. RA/LA/RV/LV for the EGM project.
        try:
            for i in range(min(len(self.lines), self.num_leads)):
                self.lines[i].set_label(self.lead_names[i])
        except Exception:
            pass
        self._populate_leads()
        if self.serial_thread:
            self.serial_thread.num_channels = self._num_channels_for(proj_key)
            self.serial_thread.adc_indexes = proj.get("adc_channels", ())
            self.serial_thread.mv_max = proj.get("mv_max", 50.0)
        # Widen the fixed vertical-scale slider to comfortably cover the
        # project's signal range (e.g. EGM cell peaks ~140 mV need more than
        # the 10 mV surface-ECG cap). Keeps the auto-scale Y range for the
        # non-ADC channels in sync with the slider as well.
        scale_max = max(10.0, proj.get("mv_max", 50.0) * 1.5)
        new_scale = min(self.amplitude_scale, scale_max)
        try:
            self.amp_scale_slider.configure(to=scale_max)
            self.amp_scale_var.set(new_scale)
            self.amplitude_scale = new_scale
            self.amp_scale_label.config(text=f'{new_scale:.1f} mV')
        except Exception:
            pass
        # Refresh the ECG legend to only the leads of the active project.
        try:
            handles, labels = self.ax.get_legend_handles_labels()
            if self.ax_legend is not None:
                self.ax_legend.remove()
            self.ax_legend = self.ax.legend(handles[:self.num_leads], labels[:self.num_leads],
                                            loc='upper right', fontsize=7, ncol=3,
                                            framealpha=0.9, edgecolor=C_BORDER)
        except Exception:
            pass

        # Clear old param widgets (any type, including styled tk widgets)
        for widget in self.params_frame.winfo_children():
            widget.destroy()

        # --- Control buttons row (styled) ---
        play_frame = tk.Frame(self.params_frame, bg=C_CARD)
        play_frame.pack(fill=tk.X, pady=(0, 6))
        _flat_btn(play_frame, '\u25b6  Play', self._send_play,
                  bg='#1D4ED8', fg='white', font=FBB,
                  abg='#1E40AF').pack(side=tk.LEFT, padx=(0, 4))
        _flat_btn(play_frame, '\u25a0  Stop', self._send_stop,
                  bg='#DC2626', fg='white', font=FBB,
                  abg='#B91C1C').pack(side=tk.LEFT, padx=(0, 4))
        _flat_btn(play_frame, '\u2699  Apply', self._apply_all_params,
                  bg=C_BTN, fg=C_TEXT, font=FBB).pack(side=tk.LEFT, padx=(0, 4))
        self.stream_btn = _flat_btn(play_frame, 'Stream OFF', self._toggle_stream,
                                    bg=C_ACCENT, fg='white', font=FBB, abg='#0F766E')
        self.stream_btn.pack(side=tk.LEFT)
        if not self.streaming:
            self._send_cmd("stream on")
        self.streaming = True

        # --- Model selector (if applicable) ---
        if "models" in proj:
            mf = tk.Frame(self.params_frame, bg=C_CARD)
            mf.pack(fill=tk.X, pady=(0, 4))
            tk.Label(mf, text='Model', bg=C_CARD, fg=C_TEXT2,
                     font=FS, width=6, anchor=tk.W).pack(side=tk.LEFT)
            self.model_var = tk.StringVar(value=proj["models"][0])
            model_combo = ttk.Combobox(mf, textvariable=self.model_var,
                                       values=proj["models"], state="readonly",
                                       font=FB)
            model_combo.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 0))
            model_combo.bind("<<ComboboxSelected>>", self._on_model_change)

        self.slider_vars.clear()
        self.slider_widgets.clear()

        # Thin separator
        tk.Frame(self.params_frame, bg=C_BORDER, height=1).pack(fill=tk.X, pady=(4, 6))

        # --- Parameter sliders (styled) ---
        for param_key, param_info in proj["params"].items():
            row_frame = tk.Frame(self.params_frame, bg=C_CARD)
            row_frame.pack(fill=tk.X, pady=2)

            tk.Label(row_frame, text=param_info["label"],
                     bg=C_CARD, fg=C_TEXT2, font=FS,
                     width=14, anchor=tk.W).pack(side=tk.LEFT)

            var = tk.DoubleVar(value=param_info["default"])
            self.slider_vars[param_key] = var

            val_label = tk.Label(row_frame,
                                 text=f"{param_info['default']:.2f} {param_info['unit']}",
                                 bg=C_CARD, fg=C_TEXT, font=FBB,
                                 width=12, anchor=tk.E)
            val_label.pack(side=tk.RIGHT)

            scale = ttk.Scale(row_frame, variable=var,
                              from_=param_info["min"], to=param_info["max"],
                              orient=tk.HORIZONTAL,
                              command=lambda v, k=param_key: self._on_slider(k))
            scale.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 4))
            scale.bind('<ButtonRelease-1>', lambda e, k=param_key: self._apply_param(k))
            self.slider_widgets[param_key] = (scale, val_label, param_info)

            help_txt = param_info.get("help")
            if help_txt:
                ToolTip(scale, help_txt)
                ToolTip(val_label, help_txt)

        # --- Rhythm selector ---
        self.rhythm_combo['values'] = proj["rhythms"]
        if proj["rhythms"]:
            self.rhythm_combo.current(0)

        # Make sure rhythm and params cards are visible
        self.params_outer.pack(fill=tk.X, padx=6, pady=3)
        self.rhythm_frame.pack(fill=tk.X, padx=6, pady=3)

    def _on_slider(self, param_key):
        if param_key in self.slider_widgets:
            _, label, info = self.slider_widgets[param_key]
            val = self.slider_vars[param_key].get()
            label.config(text=f"{val:.2f} {info['unit']}")

    def _apply_param(self, param_key):
        """Send a single slider's value to the firmware immediately on
        release (model params act on the signal only once the device applies
        them). Integers for count/percent params, floats for the rest."""
        if not self.connected:
            return
        var = self.slider_vars.get(param_key)
        if var is None:
            return
        val = var.get()
        if param_key in ("hr", "amp", "pr", "escape"):
            self._send_cmd(f"set {param_key} {int(val)}")
        else:
            self._send_cmd(f"set {param_key} {val:.3f}")

    def _on_rhythm_change(self, event=None):
        if not self.connected or not self.project_type:
            return
        if self.project_type == PICO_ECG_KEY or self.project_type not in PROJECTS:
            return
        proj = PROJECTS[self.project_type]
        idx = self.rhythm_combo.current()
        if 0 <= idx < len(proj["rhythms"]):
            was_streaming = self.streaming
            if was_streaming:
                self._send_cmd("stream off")
                self.streaming = False
                self.stream_btn.config(text="Stream ON")
                self.status_label.config(text="Changing rhythm...", foreground="orange")
                self.root.after(300, lambda: self._apply_rhythm_get(idx, was_streaming))
            else:
                self.status_label.config(text="Changing rhythm...", foreground="orange")
                self.root.after(100, lambda: self._apply_rhythm_get(idx, False))

    def _apply_rhythm_get(self, idx, resume_stream):
        self._send_cmd(f"set rhythm {idx}")
        self.root.after(400, lambda: self._do_get(resume_stream))

    def _on_model_change(self, event=None):
        if not self.connected or self.project_type not in PROJECTS:
            return
        idx = list(PROJECTS[self.project_type]["models"]).index(self.model_var.get())
        self._send_cmd(f"set model {idx}")

    def _get_settings(self):
        if not self.connected:
            return
        was_streaming = self.streaming
        if was_streaming:
            self._send_cmd("stream off")
            self.streaming = False
            self.stream_btn.config(text="Stream ON")
            self.status_label.config(text="Paused for get...", foreground="orange")
            self.root.after(300, lambda: self._do_get(was_streaming))
        else:
            self._do_get(False)

    def _do_get(self, resume_stream):
        self._send_cmd("get")
        self.root.after(800, lambda: self._finish_get(resume_stream))

    def _finish_get(self, resume_stream):
        if resume_stream and self.connected:
            self._send_cmd("stream on")
            self.streaming = True
            self.stream_btn.config(text="Stream OFF")
            self.status_label.config(text="Connected (streaming)", foreground="blue")

    def _clear_log(self):
        self.info_text.config(state=tk.NORMAL)
        self.info_text.delete("1.0", tk.END)
        self.info_text.config(state=tk.DISABLED)

    def _clear_plot(self):
        """Clear waveform buffer data queue."""
        with self.queue_lock:
            self.data_queue.clear()
        self._ring_n = 0
        self._plotted_len = 0

    def _parse_get_line(self, line):
        if "=" not in line:
            return
        key_val = line.strip()
        if "=" not in key_val:
            return
        parts = key_val.split("=", 1)
        key = parts[0].strip()
        val = parts[1].strip()
        print(f"[GET] key='{key}' val='{val}'")

        mappings = {
            "hr": self._sync_hr,
            "amp": self._sync_amp,
            "alphas": self._sync_alphas,
            "delay": self._sync_delay,
            "baseline": self._sync_baseline,
            "cal": self._sync_cal,
        }
        for pattern, handler in mappings.items():
            if key == pattern:
                handler(val)
                return

    def _sync_hr(self, val):
        try:
            bpm = int(val.split()[0])
            if "hr" in self.slider_vars:
                self.slider_vars["hr"].set(bpm)
                if "hr" in self.slider_widgets:
                    _, label, info = self.slider_widgets["hr"]
                    label.config(text=f"{bpm} {info['unit']}")
        except Exception:
            pass

    def _sync_amp(self, val):
        try:
            pct = int(val.replace("%", "").strip())
            if "amp" in self.slider_vars:
                self.slider_vars["amp"].set(pct)
                if "amp" in self.slider_widgets:
                    _, label, info = self.slider_widgets["amp"]
                    label.config(text=f"{pct} {info['unit']}")
        except Exception:
            pass

    def _sync_alphas(self, val):
        try:
            parts = val.replace("P:", "").replace("Ta:", "").replace("QRS:", "").replace("T:", "").strip()
            nums = parts.split()
            keys = ["a1", "a2", "a3", "a4"]
            for i, k in enumerate(keys):
                if i < len(nums) and k in self.slider_vars:
                    v = float(nums[i])
                    self.slider_vars[k].set(v)
                    if k in self.slider_widgets:
                        _, label, info = self.slider_widgets[k]
                        label.config(text=f"{v:.2f} {info['unit']}")
        except Exception:
            pass

    def _sync_delay(self, val):
        try:
            ms = float(val.split()[0])
            if "delay" in self.slider_vars:
                self.slider_vars["delay"].set(ms)
                if "delay" in self.slider_widgets:
                    _, label, info = self.slider_widgets["delay"]
                    label.config(text=f"{ms:.2f} {info['unit']}")
        except Exception:
            pass

    def _sync_baseline(self, val):
        try:
            parts = val.replace("mV", "@").replace("Hz", "").strip()
            nums = parts.split("@")
            if len(nums) >= 2 and "baseamp" in self.slider_vars:
                v = float(nums[0].strip())
                self.slider_vars["baseamp"].set(v)
                if "baseamp" in self.slider_widgets:
                    _, label, info = self.slider_widgets["baseamp"]
                    label.config(text=f"{v:.2f} {info['unit']}")
            if len(nums) >= 2 and "basefreq" in self.slider_vars:
                v = float(nums[1].strip())
                self.slider_vars["basefreq"].set(v)
                if "basefreq" in self.slider_widgets:
                    _, label, info = self.slider_widgets["basefreq"]
                    label.config(text=f"{v:.2f} {info['unit']}")
        except Exception:
            pass

    def _sync_cal(self, val):
        try:
            v = float(val.split()[0])
            self.cal_gain_var.set(v)
            self.cal_gain_label.config(text=f"{v:.2f}")
        except Exception:
            pass

    def _on_lead_toggle(self):
        for i, var in enumerate(self.lead_vars):
            self.visible_leads[i] = var.get()
        self._plot_dirty = True

    def _apply_all_params(self):
        if not self.connected:
            return
        for param_key, var in self.slider_vars.items():
            val = var.get()
            if param_key in ("hr", "amp"):
                self._send_cmd(f"set {param_key} {int(val)}")
            else:
                self._send_cmd(f"set {param_key} {val:.3f}")

    # ------------------------------------------------------------------
    # Display control handlers (referenced by _build_display_section)
    # ------------------------------------------------------------------

    def _on_time_change(self, val):
        self.display_seconds = float(val)
        self.time_label.config(text=f'{self.display_seconds:.1f} s')
        self._plot_dirty = True

    def _on_amp_scale_change(self, val):
        self.amplitude_scale = float(val)
        self.amp_scale_label.config(text=f'{self.amplitude_scale:.1f} mV')
        self._plot_dirty = True

    def _on_auto_scale_toggle(self):
        self.auto_scale = bool(self.auto_scale_var.get())
        self._plot_dirty = True

    # ------------------------------------------------------------------
    # Status bar
    # ------------------------------------------------------------------

    def _build_statusbar(self, parent):
        """Thin bar at the very bottom of the root window."""
        tk.Frame(parent, bg=C_BORDER, height=1).pack(fill=tk.X, side=tk.BOTTOM)
        sb = tk.Frame(parent, bg=C_BTN)
        sb.pack(fill=tk.X, side=tk.BOTTOM)
        self._sb_port = tk.Label(sb, text='No device', bg=C_BTN,
                                  fg=C_TEXT2, font=FS, padx=10, pady=3)
        self._sb_port.pack(side=tk.LEFT)
        tk.Frame(sb, bg=C_BORDER, width=1).pack(side=tk.LEFT, fill=tk.Y, pady=3)
        self._sb_rate = tk.Label(sb, text='0 samp/s', bg=C_BTN,
                                  fg=C_TEXT2, font=FS, padx=10)
        self._sb_rate.pack(side=tk.LEFT)
        tk.Frame(sb, bg=C_BORDER, width=1).pack(side=tk.LEFT, fill=tk.Y, pady=3)
        self._sb_hr_lbl = tk.Label(sb, text='HR: ---', bg=C_BTN,
                                    fg=C_TEXT2, font=FS, padx=10)
        self._sb_hr_lbl.pack(side=tk.LEFT)
        tk.Frame(sb, bg=C_BORDER, width=1).pack(side=tk.LEFT, fill=tk.Y, pady=3)
        self._sb_rec = tk.Label(sb, text='', bg=C_BTN,
                                 fg='#DC2626', font=FBB, padx=10)
        self._sb_rec.pack(side=tk.LEFT)

    def _update_stats(self):
        """Update status bar metrics every 500 ms."""
        now = time.monotonic()
        dt = now - self._last_stat_time
        if dt >= 1.0:
            self._samples_per_sec = int(
                (self._total_samples - self._last_stat_samples) / dt)
            self._last_stat_samples = self._total_samples
            self._last_stat_time = now
        try:
            self._sb_rate.config(text=f'{self._samples_per_sec} samp/s')
            if self._hr_measured > 0:
                self._sb_hr_lbl.config(
                    text=f'HR: {self._hr_measured} bpm', fg=C_ACCENT)
            else:
                self._sb_hr_lbl.config(text='HR: ---', fg=C_TEXT2)
            if self._recording:
                elapsed = time.monotonic() - self._record_t0
                self._sb_rec.config(text=f'\u23fa REC {elapsed:.0f}s')
            else:
                self._sb_rec.config(text='')
        except tk.TclError:
            pass
        self.root.after(500, self._update_stats)

    # ------------------------------------------------------------------
    # Recording
    # ------------------------------------------------------------------

    def _toggle_recording(self):
        if self._recording:
            self._stop_recording()
        else:
            self._start_recording()

    def _start_recording(self):
        self._record_buf = []
        self._record_t0 = time.monotonic()
        self._recording = True
        self._rec_btn.config(text='\u23f9  Stop Rec', bg='#DC2626',
                             fg='white', activebackground='#B91C1C')
        self._rec_status.config(text='\u23fa Recording...')

    def _stop_recording(self):
        self._recording = False
        self._rec_btn.config(text='\u23fa  Record', bg=C_BTN,
                             fg=C_TEXT, activebackground=C_BTN_HV)
        self._rec_status.config(text='')
        if not self._record_buf:
            messagebox.showinfo('Recording', 'No data was recorded.')
            return
        fname = filedialog.asksaveasfilename(
            title='Save ECG recording',
            defaultextension='.csv',
            filetypes=[('CSV files', '*.csv'), ('All files', '*.*')],
            initialfile=f'ecg_{datetime.datetime.now():%Y%m%d_%H%M%S}.csv'
        )
        if fname:
            hdr = ['t_s'] + [name.replace('(', '_').replace(')', '')
                             for name in self.lead_names[:self.num_leads]]
            if self.ppg_index is not None:
                hdr.append('PPG')
            if self.pace_mode:
                hdr += ['Atrial_V', 'Ventric_V', 'Surface_V']
            with open(fname, 'w', newline='') as f:
                w = csv.writer(f)
                w.writerow(hdr)
                w.writerows(self._record_buf)
            messagebox.showinfo(
                'Saved',
                f'Saved {len(self._record_buf)} samples to:\n{os.path.basename(fname)}')

    def _save_snapshot(self):
        fname = filedialog.asksaveasfilename(
            title='Save plot snapshot',
            defaultextension='.png',
            filetypes=[('PNG Image', '*.png'), ('All files', '*.*')],
            initialfile=f'ecg_{datetime.datetime.now():%Y%m%d_%H%M%S}.png'
        )
        if fname:
            self.fig.savefig(fname, dpi=150, bbox_inches='tight')
            messagebox.showinfo('Saved', f'Snapshot saved:\n{os.path.basename(fname)}')

    # ------------------------------------------------------------------
    # Real-time HR estimation (R-peak detection on LL channel)
    # ------------------------------------------------------------------

    def _detect_hr(self, data):
        """Estimate HR from a ventricular channel (stream index 2) using
        threshold crossing.

        Index 2 is LL(II) for the surface-ECG projects and the ventricular
        EGM (RV cell voltage) for pace_sim_intracardiac. Works on the last
        6 seconds of buffered data. Updates self._hr_measured (BPM) and
        self._hr_peak_times.
        """
        n = len(data)
        if n < self.sample_rate // 2:   # need at least 0.5 s
            return
        # Work on a sliding window (last 6 s)
        window = min(n, int(self.sample_rate * 6))
        seg = [s[2] for s in data[-window:]]   # LL(II) / RV EGM
        seg_n = len(seg)

        mean_v = sum(seg) / seg_n
        max_v  = max(seg)
        amp    = max_v - mean_v
        if amp < 0.1:   # flat signal — no peaks
            return
        threshold = mean_v + amp * 0.55

        # Simple state-machine peak detector with 200 ms refractory period
        refractory = int(self.sample_rate * 0.20)
        base_sidx  = n - window   # sample index of seg[0] in global stream
        peaks = []
        in_peak = False
        peak_val, peak_loc = -999.0, 0
        for i, v in enumerate(seg):
            if not in_peak and v > threshold:
                in_peak = True
                peak_val, peak_loc = v, i
            elif in_peak:
                if v > peak_val:
                    peak_val, peak_loc = v, i
                elif v < threshold:
                    # Check refractory against last stored peak
                    global_sidx = base_sidx + peak_loc
                    if not peaks or (global_sidx - peaks[-1]) > refractory:
                        peaks.append(global_sidx)
                    in_peak = False

        if len(peaks) < 2:
            return

        # RR intervals → BPM (use last 6 peaks)
        recent = peaks[-7:]
        rr_samps = [recent[i+1] - recent[i] for i in range(len(recent)-1)]
        mean_rr = sum(rr_samps) / len(rr_samps)
        if mean_rr > 0:
            bpm = int(round(self.sample_rate * 60.0 / mean_rr))
            if 20 <= bpm <= 400:   # sanity filter
                self._hr_measured = bpm

    def _start_plot_update(self):
        """Kick off the ~25 fps plot refresh loop."""
        self.root.after(40, self._run_plot_update)

    def _run_plot_update(self):
        try:
            self._update_plot()
        except Exception:
            pass
        try:
            self._drain_terminal_queue()
        except Exception:
            pass
        self.root.after(40, self._run_plot_update)

    def _ring_write(self, samples):
        """Absorb a chunk of new tuples into the circular numpy ring."""
        cap = RING_CAP
        total = len(samples)
        nch = min(len(samples[0]), self._ring.shape[1])
        block = np.asarray(samples, dtype=np.float64)[:, :nch]
        base = self._ring_n % cap
        first = min(total, cap - base)
        self._ring[base:base + first, :nch] = block[:first]
        if first < total:
            self._ring[:total - first, :nch] = block[first:]
        self._ring_n += total

    def _update_plot(self):
        # Ingest path (runs under the lock, briefly): copy ONLY the samples the
        # ring has not absorbed yet. The deque stays bounded, the numpy
        # rendering ring is the fast path, so the steady-state cost per frame
        # is O(new samples + rendered points) regardless of the time window.
        #
        # IMPORTANT: we compare the ring's absorbed counter against the
        # producer's MONOTONIC sample_seq, NOT len(data_queue). Once the deque
        # is trimmed (popleft at ~RING_CAP) its length plateaus even while the
        # ring keeps absorbing, so comparing against len() would report a
        # spurious "negative new samples" every frame and re-copy the whole
        # queue -> the wrapper freezes. sample_seq only ever grows, so the
        # fresh chunk stays small.
        with self.queue_lock:
            n_total = len(self.data_queue)
            prod_seq = self.serial_thread.sample_seq if self.serial_thread else n_total
            if not self.serial_thread and self._ring_n == n_total and not self._plot_dirty:
                return
            self._plot_dirty = False
            if n_total == 0:
                return
            new_count = prod_seq - self._ring_n
            if new_count <= 0:
                # Either nothing new arrived, or we are already fully caught up
                # (ring at prod_seq) with no new production: nothing to absorb.
                if prod_seq == self._ring_n:
                    return
                # Producer dropped samples we never absorbed (we stalled past a
                # trim) or the queue was cleared: re-baseline on current contents.
                new_count = n_total
                self._ring_n = prod_seq - n_total
            elif new_count > n_total:
                # We stalled long enough that some early samples were already
                # trimmed from the deque; only the newest n_total are available.
                self._ring_n = prod_seq - n_total
                new_count = n_total
            src = None
            if new_count > 0:
                src = list(islice(self.data_queue, n_total - new_count, n_total))
        # (lock released) absorb only the fresh chunk, if any
        if src:
            if self._ring is None:
                self._ring = np.zeros((RING_CAP, 16))
            if len(src) > RING_CAP:
                src = src[-RING_CAP:]
                self._ring_n = prod_seq - RING_CAP
            self._ring_write(src)
            # Keep the ring's absorbed counter in lockstep with the producer.
            with self.queue_lock:
                self._ring_n = self.serial_thread.sample_seq if self.serial_thread else self._ring_n
        self._plotted_len = self._ring_n

        # Count total samples received (for stats)
        if self.serial_thread:
            self._total_samples = self.serial_thread.total_samples_count

        # Render path: stride the ring to ~2 points/pixel and feed matplotlib
        # numpy arrays directly (no Python per-sample loops).
        nch = 9
        if self.serial_thread:
            nch = self.serial_thread.num_channels
        w = min(int(self.display_seconds * self.sample_rate), self._ring_n)
        step = max(1, int(round(w / MAX_RENDER_POINTS)))
        cap = RING_CAP
        a = (self._ring_n - w) % cap
        if a + w <= cap:
            win = self._ring[a:a + w:step, :nch]
            t = np.arange(win.shape[0], dtype=float) * (step / self.sample_rate)
        else:
            part1 = self._ring[a::step, :nch]
            count1 = cap - a                      # samples before the physical end
            remaining = w - count1                # samples to pull from the wrap
            part2 = (self._ring[:remaining:step, :nch] if remaining > 0
                     else np.empty((0, nch)))
            win = np.concatenate((part1, part2), axis=0)
            t = np.arange(win.shape[0], dtype=float) * (step / self.sample_rate)

        if win.shape[0] == 0:
            return

        for i in range(self.num_leads):
            if self.visible_leads[i]:
                self.lines[i].set_data(t, win[:, i])
            else:
                self.lines[i].set_data([], [])

        # Pace-sense ADC channels (A/V/S): plot only on the pace_sim project.
        has_pace = self.pace_mode and self.pace_indexes is not None
        self.ax_pace.set_visible(has_pace)
        self.ax_pace.set_xlim(0, self.display_seconds)
        if has_pace:
            for j, idx in enumerate(self.pace_indexes):
                if idx < nch:
                    self.pace_lines[j].set_data(t, win[:, idx])
                else:
                    self.pace_lines[j].set_data([], [])

            # Thresholds are configured at the ADC pin and are drawn over the
            # A/V traces so a capture can be correlated with the comparator.
            threshold_values = (
                self._pace_param_value('athresh', 2.7),
                self._pace_param_value('acrossback', 2.3),
                self._pace_param_value('vthresh', 2.7),
                self._pace_param_value('vcrossback', 2.3),
            )
            for line, level in zip(self.pace_threshold_lines, threshold_values):
                line.set_data([t[0], t[-1]], [level, level])

            # Live ADC readout + visible-window range per channel (in V).
            try:
                last = win[-1]
                cur = [last[i] for i in self.pace_indexes if i < nch]
                chan = win[:, [i for i in self.pace_indexes if i < nch]]
                lo = chan.min(axis=0)
                hi = chan.max(axis=0)
                self.adc_readout_lbl.config(
                    text=f'A {cur[0]:6.3f} V   V {cur[1]:6.3f} V   S {cur[2]:6.3f} V')
                self.adc_range_lbl.config(
                    text=f'A [{lo[0]:.3f}–{hi[0]:.3f}]   '
                         f'V [{lo[1]:.3f}–{hi[1]:.3f}]   '
                         f'S [{lo[2]:.3f}–{hi[2]:.3f}]')
            except Exception:
                pass
        else:
            for line in self.pace_threshold_lines:
                line.set_data([], [])

            if self.pace_fixed_scale:
                # Full 0-3.5 V scale (3.5 tops the ~3.3 V idle rail so the
                # idle-high traces are not pinned to the top edge): lets you
                # compare directly with an oscilloscope and check the true DC
                # level of the pins.
                self.ax_pace.set_ylim(0, 3.5)
            else:
                # Auto-fit the ADC axis to the visible window. The front end
                # often rides on a DC offset (e.g. ~2 V) with a small
                # attenuated ECG on top, so a fixed 0-3.5 V scale makes the
                # trace look flat.
                ca = [p for p in self.pace_indexes if p < nch]
                if ca:
                    vals = win[-200:, ca]
                    lo_v, hi_v = float(vals.min()), float(vals.max())
                    span_v = max(hi_v - lo_v, 0.05)
                    margin_v = span_v * 0.15
                    lo_v = max(0.0, lo_v - margin_v)
                    hi_v = min(3.5, hi_v + margin_v)
                    if hi_v - lo_v < 0.02:
                        hi_v = lo_v + 0.02
                    self.ax_pace.set_ylim(lo_v, hi_v)

        # Show the time axis only on the bottom-most visible subplot: the
        # pace ADC panel when in pace_sim mode, otherwise the ECG panel.
        if has_pace:
            for label in self.ax.get_xticklabels():
                label.set_visible(False)
            self.ax.set_xlabel("")
            for label in self.ax_pace.get_xticklabels():
                label.set_visible(True)
            self.ax_pace.set_xlabel("Time (s)")
        else:
            for label in self.ax.get_xticklabels():
                label.set_visible(True)
            self.ax.set_xlabel("Time (s)")

        self.ax.set_xlim(0, self.display_seconds)

        if self.auto_scale:
            vis = [i for i in range(self.num_leads) if self.visible_leads[i]]
            if vis:
                sub = win[-200:, vis]
                ymin, ymax = float(sub.min()), float(sub.max())
                margin = max(0.1, (ymax - ymin) * 0.15)
                self.ax.set_ylim(ymin - margin, ymax + margin)
        else:
            self.ax.set_ylim(-self.amplitude_scale, self.amplitude_scale)

        # --- Real-time HR estimation (~1 s cadence, on the FULL-rate deque so
        # decimation does not clip the R spike below the threshold) ---
        now_t = time.monotonic()
        if now_t - self._last_hr_t >= 1.0:
            self._last_hr_t = now_t
            rate = self.sample_rate
            with self.queue_lock:
                cur = len(self.data_queue)
                seg_raw = (list(islice(self.data_queue, max(0, cur - 6 * rate), cur))
                           if cur >= rate // 2 else [])
            if seg_raw:
                self._detect_hr(seg_raw)
        if self._hr_measured > 0:
            self._hr_text.set_text(f'{self._hr_measured} bpm')
        else:
            self._hr_text.set_text('')

        # --- CSV recording (full-rate tail) ---
        if self._recording and src:
            # Append the last sample with a timestamp
            t0 = self._record_t0
            for k, sample in enumerate(src[-min(len(src), 30):]):
                ts = round((time.monotonic() - t0) - (len(src) - k - 1)/self.sample_rate, 6)
                if ts < 0:
                    ts = 0.0
                row = [ts] + list(sample[:self.num_leads])
                if self.ppg_index is not None and len(sample) > self.ppg_index:
                    row.append(sample[self.ppg_index])
                if self.pace_mode and self.pace_indexes is not None and len(sample) > self.pace_indexes[-1]:
                    row += [sample[i] for i in self.pace_indexes]
                self._record_buf.append(row)

        self.canvas.draw_idle()


# =============================================================================
# Entry Point
# =============================================================================

def main():
    root = tk.Tk()
    app = ECGApp(root)

    def on_closing():
        app._shutdown()

    root.protocol("WM_DELETE_WINDOW", on_closing)
    root.mainloop()

    # Tk mainloop returned: tear down matplotlib canvases and force-exit the
    # process. The packaged app is PyInstaller --onefile, whose child process
    # otherwise stays alive in Task Manager if any library thread lingers.
    try:
        import matplotlib.pyplot as plt
        plt.close('all')
    except Exception:
        pass
    os._exit(0)


if __name__ == "__main__":
    main()
