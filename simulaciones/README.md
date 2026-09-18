# ⚡ Simulaciones de Circuitos y Filtros

Modelos en **LTSpice** y scripts de procesamiento en **Python** para el diseño, simulación y validación de las etapas analógicas del Fantoma ECG.

---

## 📁 Contenido del Directorio

### 1. Modelos de Circuito (`ltspice/`)

* **`filter_pwm.asc`**: Simulación transitoria en el dominio del tiempo del filtro pasa-bajos activo/pasivo. Modela la reconstrucción analógica continua a partir de la señal modulada por ancho de pulsos (PWM) generada por el microcontrolador (RP2040 / RP2350).
* **`filter_pwm_ac.asc`**: Análisis en el dominio de la frecuencia (barrido AC / Diagrama de Bode) para determinar la frecuencia de corte (-3 dB), la linealidad en la banda de paso médica (0.05 Hz – 150 Hz) y la atenuación de la frecuencia portadora PWM.
* **`pace_input_circuit.asc`**: Etapa analógica de acondicionamiento y sensado para detección de impulsos y espigas de marcapasos artificiales.

### 2. Estímulos y Tablas de Tensión PWL (`ltspice/`)

* **`ecg_pwl.txt`**: Datos de señal electrocardíaca formateados en pares `(tiempo, voltaje)` para alimentar fuentes dependientes `PWL` en simulaciones transitorias.
* **`ECG-LTSPICE.txt`**: Trazado cardíaco en sintaxis nativa y compacta de LTSpice `(t1 v1 t2 v2 ...)`.
* **`pace_pulse_train_rc.txt`**: Tren de pulsos periódicos de alta tensión con decaimiento RC característico para probar la respuesta del circuito de detección de marcapasos.

### 3. Scripts de Análisis y Visualización (`scripts/`)

* **`data_graph.py`**: Script de Python optimizado para procesar grandes volúmenes de datos exportados desde LTSpice:
  * Emplea el motor C de `pandas` y tabuladores para lectura rápida.
  * Implementa **submuestreo inteligente** (`MAX_PUNTOS = 50000`) para renderizado fluido de curvas de alta resolución sin saturar la memoria.
  * Utiliza backend `TkAgg` para compatibilidad en entornos VS Code / Anaconda.
  * Genera dos vistas clave:
    1. **Desglose individual:** Visualización multicanal de tensiones intermedias y nodos del circuito.
    2. **Comparación de fidelidad:** Superposición directa entre la señal de referencia/original (`V(v_sig)`) y la señal analógica filtrada (`V(n002)`).
* **`Figure_1.png`**: Gráfica resultante del análisis temporal por canal individual.
* **`Figure_2.png`**: Gráfica comparativa de reconstrucción de bioseñal (Original vs. Filtrada).

---

## 📊 Resultados de Simulación

| Desglose de Nodos (Figura 1) | Reconstrucción Original vs. Filtrada (Figura 2) |
|:---:|:---:|
| ![Análisis individual](scripts/Figure_1.png) | ![Comparación](scripts/Figure_2.png) |

---

## 🚀 Flujo de Trabajo para Replicar Simulaciones

1. **Abrir y simular en LTSpice:**
   - Abre `ltspice/filter_pwm.asc` en LTSpice.
   - Ejecuta la simulación (`Run` / Transient Analysis).
2. **Exportar datos desde LTSpice:**
   - En la ventana de formas de onda: `File` → `Export data as text`.
   - Selecciona las variables de interés (`V(v_sig)`, `V(n002)`, etc.).
   - Guarda el archivo exportado con el nombre `filter_pwm_out_signals.txt` dentro de `simulaciones/scripts/`.
3. **Generar gráficos:**
   ```bash
   cd simulaciones/scripts
   python data_graph.py
   ```

> [!NOTE]
> Los archivos de simulación transitoria de gran tamaño (como `filter_pwm_out_signals.txt` que puede superar los 800 MB, así como los binarios `.raw` y `.log` de LTSpice) están omitidos por `.gitignore` para evitar sobrecargar el repositorio Git.
