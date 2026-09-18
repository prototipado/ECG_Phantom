# 💻 Software Host en Python (GUI de Monitoreo y Control)

Aplicación de escritorio desarrollada en Python (**Tkinter + Matplotlib**) para la conexión en tiempo real, configuración de parámetros y visualización multicanal de señales del Fantoma ECG.

---

## ⚡ Características Principales

- **Streaming serie USB de alta velocidad:** Conexión a **921600 baudios** con recepción y desempacado de tramas binarias (`0xAA 0x55 ... 0x55 0xAA`) a tasas de hasta **2000 SPS**.
- **Handshake y autodetección de modelo:** Identifica dinámicamente el firmware conectado mediante el comando CLI `ver` y reconfigura los deslizadores, derivaciones y opciones disponibles:
  - *Conduction Model* (Quiroz-Juárez 2018: Van der Pol + FitzHugh-Nagumo).
  - *Gaussian ECG + PPG* (Huynh/Tran 2026: 9 derivaciones ECG + canal óptico PPG).
  - *Pace Sim / Intracardiac Bench* (bancos de prueba con canales ADC para espigas de marcapasos y electrogramas EGM).
  - *pico_ecg* (reproductor de registros clínicos en bucle).
- **Visualizador multicanal optimizado:** Renderizado con búfer circular NumPy y **decimación inteligente**, evitando saturar la interfaz gráfica incluso en ventanas de tiempo prolongadas (1 a 30 s).
- **Control dinámico de parámetros:** Deslizadores con actualización inmediata (`> set <param> <valor>`) y *tooltips* explicativos sobre cada parámetro electrofisiológico.
- **Detección Pace-sense (Banco de Marcapasos):** Panel dedicado para calibración de umbrales atriales/ventriculares (`athresh`, `vthresh`, `acrossback`, `vcrossback`), tiempos de rebote y períodos refractarios.
- **Grabación y exportación de datos:**
  - ⏺ **Record CSV:** Registro continuo de todas las muestras con marcas de tiempo en microsegundos.
  - 📷 **Snapshot PNG:** Exportación de capturas vectoriales/rasterizadas en alta resolución (150 DPI).
- **Terminal de datos integrado:** Monitor serie en vivo con decodificación de tramas y visor hexadecimal.
- **Medición de HR (BPM):** Algoritmo de detección de picos R con cálculo de frecuencia cardíaca instantánea.

---

## 🚀 Ejecución desde Código Fuente (Python)

### 1. Requisitos previos
- Python 3.9 o superior.
- Se recomienda el uso de un entorno virtual:

```bash
cd software
python -m venv .venv

# En Windows:
.venv\Scripts\activate

# En Linux / macOS:
source .venv/bin/activate
```

### 2. Instalar dependencias
```bash
pip install -r requirements.txt
```

### 3. Iniciar la aplicación
```bash
python ecg_gui.py
```

---

## 📦 Compilación a Ejecutable Independiente (.exe)

Para distribuir la aplicación a usuarios que no dispongan de Python instalado en Windows:

1. Ejecuta el script automatizado de compilación:
   ```cmd
   build_exe.bat
   ```
2. El script verificará las dependencias (`PyInstaller`), compilará la aplicación en modo ventana de un solo archivo (`--onefile --windowed`) y generará el ejecutable en:
   ```
   software/dist/ECG_GUI.exe
   ```

> [!NOTE]
> La carpeta `dist/` y los ejecutables `.exe` están excluidos del repositorio Git mediante `.gitignore` para no sobrecargar el historial con binarios pesados (~47 MB). Se recomienda publicar los binarios compilados en la sección de **Releases** de GitHub.
