# ⚡ Simulaciones de Circuitos y Filtros

Esta carpeta contiene los esquemáticos de simulación SPICE y scripts auxiliares para el modelado y verificación de las etapas analógicas del Fantoma ECG.

## 📁 Contenido

- **`ltspice/`**:
  - `filter_pwm.asc`: Filtros pasabajos activos/pasivos para la reconstrucción de la señal analógica a partir de salidas DAC / PWM del microcontrolador.
  - `pace_input_circuit.asc`: Etapa de sensado y acondicionamiento para pulsos y espigas de marcapasos artificiales.
  - `ecg_pwl.txt`: Estímulos de tensión por tabla PWL (Piecewise Linear) generados a partir de bases de datos clínicas para alimentar las simulaciones.
- **`scripts/`**:
  - `data_graph.py`: Script de Python para análisis de señales, cálculo de respuesta en frecuencia (Bode) y distorsión armónica/atenuación.

## 🛠️ Requisitos de Simulación

- **LTSpice XVII o superior** / QSPICE.
- Python 3.9+ con `numpy`, `matplotlib`, `scipy` para los scripts de análisis.
