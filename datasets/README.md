# 📊 Bases de Datos y Registros Clínicos (Datasets)

Este directorio contiene las bases de datos clínicas originales, los scripts de preprocesamiento y extracción de ciclos cardíacos, y las tablas de datos en C (`.h`) listas para ser embebidas en la memoria Flash/RAM de los firmwares del microcontrolador.

---

## 📁 Estructura del Directorio

```
datasets/
├── raw/                      # Registros clínicos originales completos
│   └── lobachevsky-university-electrocardiography-database-1.0.1/
├── scripts/                  # Herramientas de extracción, filtrado y conversión
│   └── generacion_de_senales.ipynb
├── header_arrays/            # Tablas C (.h) generadas para el firmware
│   ├── Sinus_rhythm.h
│   ├── Sinus_bradycardia.h
│   ├── Atrial_fibrillation_N_51.h
│   └── ...
└── processed/                # Señales intermedias procesadas (CSV / ODS)
```

---

## 🏥 1. Datos Clínicos Originales (`raw/`)

* **`lobachevsky-university-electrocardiography-database-1.0.1/` (LUDB)**:
  * Base de datos clínica de referencia alojada en PhysioNet.
  * Contiene 200 registros electrocardiográficos estándar de **12 derivaciones** (`I`, `II`, `III`, `aVR`, `aVL`, `aVF`, `V1`–`V6`).
  * Incluye anotaciones detalladas de ritmos, anomalías de conducción, hipertrofias, isquemia y extrasístoles (`ludb.csv`).

---

## ⚙️ 2. Scripts de Conversión (`scripts/`)

* **`generacion_de_senales.ipynb`**:
  * Emplea la librería `wfdb` para la lectura directa de los registros de PhysioNet.
  * Detecta los picos QRS y analiza las derivaciones para aislar **ciclos cardíacos completos**.
  * Algoritmo de empatamiento de extremos: selecciona el ciclo donde la amplitud inicial es lo más cercana posible a la amplitud final, garantizando una **reproducción continua en bucle (loop) sin discontinuidades ni artefactos**.
  * Aplica offset y factor de escala para DAC / PWM de 12 a 16 bits.
  * Exporta automáticamente el arreglo multidimensional a un archivo de cabecera en C (`.h`) dentro de `header_arrays/`.

---

## 🧬 3. Tablas de Cabecera C (`header_arrays/`)

Archivos `.h` formateados para compilación con el SDK de Raspberry Pi Pico (C/C++):

| Archivo | Patología / Ritmo | Paciente ID | Descripción |
|---|---|---|---|
| **`Sinus_rhythm.h`** | Ritmo Sinusal Normal | 1 | Trazado sinusal de referencia |
| **`Sinus_rhythm_N_*.h`** | Ritmo Sinusal Normal | 11, 21, 31, 45, 46, 55, 66 | Variaciones morfológicas individuales |
| **`Sinus_bradycardia.h`** | Bradicardia Sinusal | Base | Frecuencia cardíaca baja (<60 BPM) |
| **`Sinus_bradycardia_N_*.h`**| Bradicardia Sinusal | 41, 47 | Casos clínicos con bradicardia |
| **`Atrial_fibrillation_N_51.h`**| Fibrilación Auricular | 51 | Pérdida de onda P y ritmo irregular |

### Formato de datos en C:
Cada archivo define una estructura `ecg_struct_t` con metadatos clínicos y la matriz de datos:
```c
#include "ecg_types.h"

#define ROWS_46 391
#define COLS_46 12

static const int ecg_array[ROWS_46][COLS_46] = {
    {30058, 29998, 29938, ...},
    ...
};

ecg_struct_t Sinus_rhythm_N_46 = {
    .rhythms = "SINUS RHYTHM",
    .cond_abnor = "NONE",
    .rows = ROWS_46,
    .cols = COLS_46,
    .array = ecg_array
};
```
