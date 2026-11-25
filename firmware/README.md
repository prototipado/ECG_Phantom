# Firmwares de Emulación Cardíaca (RP2040 / RP2350)

Este directorio alberga los módulos de firmware diseñados para microcontroladores Raspberry Pi Pico y Pico 2 (RP2040 / RP2350). Cada firmware aborda un método distinto de síntesis o reproducción de señales electrofisiológicas.

## Matriz de firmwares

| Carpeta | Nombre / Enfoque | Estado | Método de Generación | Salidas Soportadas | Complejidad Computacional |
|---|---|---|---|---|---|
| **`01_pico_ecg`** | Reproductor Clínico | **Disponible** | Lectura directa de Flash/RAM (PhysioNet/LUDB) | 1 a 12 derivaciones (9 electrodos analógicos) | Baja (Look-up Table / DMA) |
| **`02_pico_conduction_model`** | Modelo Electrofisiológico | **Disponible** | Ecuaciones diferenciales acopladas (Van der Pol + FitzHugh-Nagumo) | 12 derivaciones estándar (9 electrodos analógicos) | Media-Alta (Cálculo numérico RK4/Euler a 2 kHz) |
| **`03_pico_ecg_gaussian_ppg`** | Síntesis Analítica + PPG | **Disponible** | Suma de kernels Gaussianos dinámicos evento-driven | 12 derivaciones ECG (referenciadas a RA) + 2 canales PPG | Media (Evaluación analítica en tiempo real a 2 kHz) |
| **`04_pico_multimodel`** | Suite Multi-Motor 4-en-1 | *En desarrollo* | Selector de motor matemático en caliente (VdP+FHN, BVAM, anillo VdP, Gaussianas cuasiperiódicas) | 12 derivaciones (modelo heterogéneo) / Lead II (otros 3 modelos) | Alta |
| **`05_pace_sim_intracardiac_9_nodes`** | Simulador de Marcapasos / EGM Intracardíaco | *En desarrollo* | Red de conducción de 9 nodos + autómata celular, captura de pulsos externos por ADC | EGM auricular/ventricular (PWM + streaming 7 canales) | Alta |

## Módulo inicial: `01_pico_ecg`

Plataforma de reproducción de señales clínicas reales almacenadas en memoria Flash:
- **Salida multicanal:** 9 potenciales analógicos de electrodos (RA, LA, LL, V1–V6) generados vía moduladores PWM a 100 kHz.
- **Interfaz local:** Menú gráfico en pantalla OLED (SSD1306/SH1106 vía SPI) administrado con encoder rotativo mediante la librería GEM y u8g2.
- **Consola interactiva CLI USB:** Comandos serie (`> set`, `> get`, `> list`, `> play`, `> stop`, `> stream on/off`, `> ver`).
- **Controles de salida ECG:** Offset DC (`-16384..16384`) y Cal Gain (`0.01..100x`) para registros guardados; consultables con `> get offset` y `> get cal`.
- **Streaming de alta velocidad:** Transmisión binaria IEEE 754 a 500 SPS compatible con `software/ecg_gui.py`.
- **Indicación auditiva y visual:** LED indicador y buzzer piezoeléctrico sincronizados con la onda R (complejo QRS).
- *Documentación detallada, pinout completo y comandos:* Consultar [firmware/01_pico_ecg/README.md](./01_pico_ecg/README.md).

## Módulo 02: `02_pico_conduction_model`

Firmware de síntesis electrofisiológica mediante el modelo de conducción cardíaca de osciladores heterogéneos acoplados (Quiroz-Juarez et al., *"Cardiac Conduction Model for Generating 12 Lead ECG Signals With Realistic Heart Rate Dynamics"*, IEEE Trans. Nanobioscience 2018):
- **Modelo biofísico:** Dos subsistemas acoplados que integran marcapasos naturales de Van der Pol (nodos SA, AV, His-Purkinje) con osciladores de FitzHugh-Nagumo para la respuesta muscular atrial/ventricular (ondas P, Ta, complejo QRS y onda T).
- **Salida multicanal analógica:** 9 potenciales de electrodos independientes (RA, LA, LL, V1–V6) modulados por PWM a 100 kHz en Core 1 integrando a 2000 Hz.
- **Autocalibración por hardware:** Al encender, mide la función de transferencia del filtro pasabajos de RA mediante ADC0 (GPIO26) e inyección senoidal, ajustando automáticamente la ganancia y el offset para maximizar el rango dinámico sin saturar el amplificador operacional.
- **Streaming USB a 2 kHz:** Transmisión binaria en tiempo real (9 canales float32, 40 bytes/muestra a 921600 baudios) para visualización fluida en la GUI de escritorio.
- **Dinámica fisiológica:** Simulación de variabilidad de la frecuencia cardíaca (HRV con componentes respiratorios RSA a 0.25 Hz y ondas de Mayer a 0.1 Hz), oscilación de línea base acoplada y patologías (bradicardia, taquicardia, bloqueos AV de 1° a 3° grado).
- *Documentación completa, pinout y parámetros:* Consultar [firmware/02_pico_conduction_model/README.md](./02_pico_conduction_model/README.md).

## Módulo 03: `03_pico_ecg_gaussian_ppg`

Firmware de síntesis analítica simultánea de ECG y pulso óptico (PPG) basado en el modelo evento-driven con kernels Gaussianos (Huynh, Tran & Tran, *"An ECG-PPG Physiological Signal Emulator for Calibration and Validation of Cardiovascular Monitoring Devices"*, Sensors 2026, 26, 5578):
- **Modelo analítico y acoplamiento ECG-PPG:** Generación de un vector cardíaco 3D proyectado a las 12 derivaciones estándar junto con la propagación electromecánica de la onda de pulso arterial hacia la periferia (PEP + tiempo de tránsito PWV), conformada por 5 Gaussianas (pico sistólico, hombro, muesca dicrótica, diastólico y cola).
- **Mapeo de salida referenciado a RA:** Salidas analógicas generadas con respecto a RA (GPIO1 = Derivación I, GPIO2 = Derivación II, GPIO3/4/6-9 = V1-V6) para que un monitor o electrocardiógrafo comercial mida las 12 derivaciones exactas con su terminal RA puesto a GND (referencia común en RL).
- **Doble canal PPG:** Salida principal en GPIO0 para excitación de diodo emisor SpO2 (con rango de tensión seguro medido por calibración) y salida duplicada de monitorización en GPIO10 apta para osciloscopio.
- **Autocalibración por ADC0:** Calibración de amplitud y offset sobre la derivación I (GPIO1) mediante lectura con ADC0 (GPIO26).
- **Streaming USB a 2 kHz:** Paquete de 10 canales en punto flotante IEEE 754 (44 bytes/muestra: potencial RA de referencia, I, II, V1–V6 y canal PPG) procesados por la GUI para graficar las 12 derivaciones reconstruidas y el pulso PPG acoplado.
- **Control dinámico:** Ajuste continuo de frecuencia cardíaca, amplitudes de ondas (P, R, T, PPG), intervalos PR y QT dependientes del ritmo, modulación respiratoria (RSA) y arritmias.
- *Documentación y detalle del modelo matemático:* Consultar [firmware/03_pico_ecg_gaussian_ppg/README.md](./03_pico_ecg_gaussian_ppg/README.md).

## Módulo 04: `04_pico_multimodel` *(Próxima incorporación)*

Suite de cuatro modelos matemáticos de dinámica cardíaca seleccionables en caliente (Quiroz-Juarez et al., *"ECG Patient Simulator Based on Mathematical Models"*, Sensors 2022, 22, 5714):
- Comparte GEM y u8g2 desde `common/`; no incorpora copias locales de esas librerías.
- Modelos disponibles: Osciladores Heterogéneos (12 derivaciones), Reacción-Difusión BVAM, Anillo de 3 Osciladores Van der Pol y Movimiento Cuasiperiódico (estos 3 últimos, solo Lead II replicado al resto de canales).
- Ejecuta al encender la misma autocalibración de RA por ADC0/GPIO26 que `02_pico_conduction_model`, midiendo el rango del modelo activo al arrancar para fijar Offset/Cal Gain automáticos.
- Limita la salida ECG al margen medido del op-amp; las ondas de prueba mantienen su ruta original.
- *Documentación, normalización por modelo y calibración:* Consultar [firmware/04_pico_multimodel/README.md](./04_pico_multimodel/README.md).

## Módulo 05: `05_pace_sim_intracardiac_9_nodes` *(Próxima incorporación)*

Simulador de electrogramas intracavitarios (EGM) y banco de pruebas de marcapasos en lazo cerrado:
- Comparte GEM y u8g2 desde `common/`; no incorpora copias locales de esas librerías.
- Implementa una red reducida de 9 nodos (SA, RA, LA, AV, His, RBB, LBB, RV, LV) con 11 caminos configurables y un autómata celular de 5 estados para las salidas EGM.
- Acepta pulsos del DUT por ADC (auricular, ventricular y superficie) y reporta captura/sin captura dentro de la trama de streaming de 32 bytes a 2 kHz.
- *Documentación del modelo, CLI, streaming y pruebas:* Consultar [firmware/05_pace_sim_intracardiac_9_nodes/README.md](./05_pace_sim_intracardiac_9_nodes/README.md).

## Componentes compartidos (`common/`)

Para evitar duplicar código, el directorio `common/` centraliza módulos compartidos configurados como librerías estáticas de CMake:

```text
firmware/
├── common/
│   ├── CMakeLists.txt     # Define los targets estáticos 'u8g2' y 'gem'
│   ├── GEM/               # Graphic Embedded Menu
│   └── u8g2/              # Librería gráfica u8g2/u8x8
├── 01_pico_ecg/           # Firmware de reproducción
├── 02_pico_conduction_model/ # Síntesis por modelo de conducción
├── 03_pico_ecg_gaussian_ppg/ # Síntesis analítica ECG-PPG por kernels Gaussianos
├── 04_pico_multimodel/    # Suite de 4 modelos seleccionables en caliente
└── 05_pace_sim_intracardiac_9_nodes/ # Simulador de marcapasos / EGM intracardíaco
```

### Targets disponibles en `common`

1. **`u8g2`**: Compila el motor gráfico universal de displays monocromáticos.
2. **`gem`**: Compila el sistema de menús embebidos interactivo para encoders. Enlaza a `u8g2` de forma `PUBLIC`, heredando sus archivos de cabecera.

### Cómo integrar `common` en un nuevo firmware

Para crear un nuevo firmware (ej. `02_pico_conduction_model`) que haga uso de GEM y u8g2, en su `CMakeLists.txt` se debe:

```cmake
# 1. Registrar el subdirectorio 'common'
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_CURRENT_BINARY_DIR}/common_build)

# 2. Agregar el ejecutable
add_executable(mi_firmware 
    main.c
)

# 3. Vincular 'gem' (propaga u8g2 automáticamente)
target_link_libraries(mi_firmware 
    gem
    pico_stdlib
    hardware_spi
)
```

Nota: CMake propaga automáticamente las rutas de include públicas (`common/GEM` y `common/u8g2/csrc`) a cualquier ejecutable que enlace la librería `gem` o `u8g2`.

## Guía de compilación

Todos los firmwares utilizan el Raspberry Pi Pico SDK (v2.x) y CMake.

### 1. Requisitos previos

- Raspberry Pi Pico SDK 2.x instalado (`PICO_SDK_PATH`).
- Toolchain GCC para ARM Cortex-M: `arm-none-eabi-gcc` (v12 o superior).
- CMake (v3.13 o superior) y Ninja (recomendado).
- Opcional: Visual Studio Code con la extensión oficial de Raspberry Pi Pico.

### 2. Compilación desde la terminal

Navegar a la carpeta del firmware que se desea compilar:

```powershell
# Entrar al firmware
cd firmware/01_pico_ecg

# Generar los archivos de compilación
cmake -G Ninja -B build -S .

# Compilar
ninja -C build
```

Si no se dispone de Ninja:
```powershell
cmake -B build -S .
cmake --build build
```

### 3. Compilación desde Visual Studio Code

1. Abrir la carpeta del firmware (por ejemplo `firmware/01_pico_ecg`) en VS Code.
2. La extensión de Raspberry Pi Pico detectará el archivo `.vscode/settings.json`.
3. Presionar `Ctrl + Shift + B` o hacer clic en "Compile Project".

### 4. Carga en la placa (flasheo)

En la carpeta `build/` se generará un archivo `.uf2`:

1. Conectar la Raspberry Pi Pico / Pico 2 a la PC manteniendo presionado el botón BOOTSEL.
2. Aparecerá una unidad de almacenamiento USB (`RPI-RP2` o `RP2350`).
3. Copiar el archivo `.uf2` dentro de la unidad.
4. El microcontrolador se reiniciará automáticamente.
