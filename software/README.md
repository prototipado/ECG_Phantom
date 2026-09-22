# Software Host en Python (GUI de Monitoreo y Control)

Aplicación de escritorio desarrollada en Python (Tkinter + Matplotlib) para la conexión, configuración de parámetros y visualización multicanal de señales del Fantoma ECG.

## Características principales

- **Streaming serie USB**: Comunicación a 921600 baudios para recepción de tramas binarias a tasas de hasta 2000 SPS.
- **Autodetección de firmware**: Identifica el módulo de hardware conectado y adapta los controles y derivaciones disponibles (reproducción clínica, modelos electrofisiológicos, síntesis, etc.).
- **Visualizador multicanal**: Renderizado de señales utilizando búferes de NumPy y submuestreo para mantener fluidez en diferentes ventanas de tiempo.
- **Control de parámetros**: Ajuste en tiempo real de variables electrofisiológicas.
- **Detección de marcapasos**: Panel de configuración de umbrales atriales/ventriculares y tiempos refractarios (disponible según el firmware).
- **Registro de datos**:
  - Exportación de muestras a CSV con marcas de tiempo en microsegundos.
  - Guardado de capturas de pantalla de las gráficas.
- **Terminal serie**: Monitor integrado para ver la transmisión de datos y comandos.
- **Cálculo de frecuencia cardíaca**: Detección de picos R y estimación de BPM en tiempo real.

## Ejecución desde código fuente (Python)

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

## Compilación a ejecutable independiente (.exe)

Para distribuir la aplicación a usuarios de Windows sin requerir instalación de Python:

1. Ejecutar el script automatizado de compilación:
   ```cmd
   build_exe.bat
   ```
2. El script verificará las dependencias (PyInstaller) y generará un ejecutable en:
   ```
   software/dist/ECG_GUI.exe
   ```

Nota: La carpeta `dist/` y los ejecutables `.exe` están excluidos del repositorio Git.

