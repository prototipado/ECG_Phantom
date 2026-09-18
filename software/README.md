# 💻 Software Host en Python (GUI de Monitoreo y Control)

Suite de escritorio para el control en tiempo real, configuración de parámetros y visualización multicanal de señales adquiridas o transmitidas por el Fantoma ECG.

## 📦 Estructura del Código Fuente

- **`assets/`**: Iconos, fuentes y esquemas visuales de la interfaz de usuario.
- **`src/gui/`**: Componentes visuales, paneles de control, widgets de osciloscopio y gestión de gráficos.
- **`src/comms/`**: Driver serie USB para comunicación bidireccional con el microcontrolador (RP2040/RP2350).
- **`src/processing/`**: Algoritmos de filtrado digital (filtros notch 50/60 Hz, pasa-banda), cálculo de frecuencia cardíaca (BPM) y exportación de datos.
- **`ecg_gui.py`**: Punto de entrada de la aplicación.

## 🚀 Instalación y Uso

1. Crear y activar entorno virtual:
   ```bash
   python -m venv .venv
   # Windows:
   .venv\Scripts\activate
   # Linux / macOS:
   source .venv/bin/activate
   ```

2. Instalar dependencias:
   ```bash
   pip install -r requirements.txt
   ```

3. Ejecutar la aplicación:
   ```bash
   python ecg_gui.py
   ```
