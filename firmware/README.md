# 🧠 Firmwares de Emulación Cardíaca (RP2040 / RP2350)

Este directorio alberga los módulos de firmware diseñados para microcontroladores Raspberry Pi Pico y Pico 2 (RP2040 / RP2350). Cada firmware aborda un método distinto de síntesis o reproducción de señales electrofisiológicas.

## 📊 Matriz Comparativa de Firmwares

| Carpeta | Nombre / Enfoque | Estado | Método de Generación | Salidas Soportadas | Complejidad Computacional |
|---|---|---|---|---|---|
| **`01_pico_ecg`** | Reproductor Clínico | ✅ **Disponible** | Lectura directa de Flash/RAM (PhysioNet) | 1 a 12 derivaciones | Baja (Look-up Table / DMA) |
| **`02_pico_conduction_model`** | Modelo Electrofisiológico | ⏳ *En desarrollo* | Ecuaciones diferenciales acopladas (Van der Pol + FHN) | Derivaciones bipolares / derivaciones sintéticas | Media-Alta (Cálculo numérico RK4/Euler) |
| **`03_pico_gaussian_ppg`** | Síntesis Analítica + PPG | ⏳ *Planificado* | Suma de curvas Gaussianas dinámicas | 9 derivaciones ECG + Pulso Óptico PPG | Media |
| **`04_pico_multimodel`** | Suite Multi-Motor 4-en-1 | ⏳ *Planificado* | Selector de motor matemático en caliente vía menú/UART | Multicanal configurable | Alta |
| **`05_pace_sim`** | Simulador de Marcapasos | ⏳ *Planificado* | Inyección de espigas analógicas/digitales con tiempos configurables | Salida de estimulación cardíaca | Baja-Media |

---

## 🗂️ Módulo Inicial: [`01_pico_ecg`](./01_pico_ecg/)

Plataforma de reproducción de señales clínicas reales almacenadas en memoria Flash:
- **Salida multicanal:** 9 potenciales analógicos de electrodos (**RA, LA, LL, V1–V6**) generados vía moduladores PWM a 100 kHz.
- **Interfaz local:** Menú gráfico en pantalla OLED (SSD1306/SH1106 vía SPI) administrado con encoder rotativo mediante la librería **GEM** y **u8g2**.
- **Consola interactiva CLI USB:** Comandos serie (`> set`, `> get`, `> list`, `> play`, `> stop`, `> stream on/off`, `> ver`).
- **Streaming de alta velocidad:** Transmisión binaria IEEE 754 a 500 SPS compatible con `software/ecg_gui.py`.
- **Feedback audiovisual:** LED indicador y buzzer piezoeléctrico sincronizados con la onda R (complejo QRS).
- *Documentación detallada, pinout completo y comandos:* Consultar [firmware/01_pico_ecg/README.md](./01_pico_ecg/README.md).

---

## 📦 Componentes Compartidos (`common/`)

Para evitar duplicar código, librerías y fuentes en cada firmware, el directorio **[`common/`](./common/)** centraliza módulos compartidos configurados como librerías estáticas de CMake:

```text
firmware/
├── common/
│   ├── CMakeLists.txt     # Define los targets estáticos 'u8g2' y 'gem' con headers públicos
│   ├── GEM/               # Graphic Embedded Menu (núcleo en C y adaptadores)
│   └── u8g2/              # Librería gráfica monocromática u8g2/u8x8
├── 01_pico_ecg/           # Firmware 1 (reproductor clínico)
└── 02_.../                # Futuros firmwares
```

### 🎯 Targets Disponibles en `common`

1. **`u8g2`**: Compila el motor gráfico universal de displays monocromáticos (SSD1306, SH1106, etc.) con sus fuentes optimizadas.
2. **`gem`**: Compila el sistema de menús embebidos interactivo para encoders y pantallas gráficas. Ya enlaza a `u8g2` de forma `PUBLIC`, por lo que hereda automáticamente todos sus archivos de cabecera.

### 🔌 Cómo Integrar `common` en un Nuevo Firmware

Para crear un nuevo firmware (ej. `02_pico_conduction_model`) que haga uso de `GEM` y `u8g2`, en su `CMakeLists.txt` solo debes:

```cmake
# 1. Registrar el subdirectorio 'common' indicando su carpeta binaria
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_CURRENT_BINARY_DIR}/common_build)

# 2. Agregar tu ejecutable con tus archivos fuente propios
add_executable(mi_firmware 
    main.c
    # ... tus fuentes de drivers, modelos, etc.
)

# 3. Vincular 'gem' (propaga u8g2 e includes automáticamente)
target_link_libraries(mi_firmware 
    gem
    pico_stdlib
    hardware_spi
    # ... otras librerías del Pico SDK necesarias
)
```

> [!TIP]
> **No necesitas añadir `target_include_directories` manuales para GEM ni u8g2**: CMake propaga automáticamente las rutas de include públicas (`common/GEM` y `common/u8g2/csrc`) a cualquier ejecutable que enlace la librería `gem` o `u8g2`.

---

## 🛠️ Guía de Compilación

Todos los firmwares utilizan el **Raspberry Pi Pico SDK (v2.x)** y **CMake**.

### 1. Requisitos Previos

- **Raspberry Pi Pico SDK 2.x** instalado (`PICO_SDK_PATH`).
- **Toolchain GCC para ARM Cortex-M**: `arm-none-eabi-gcc` (v12 o superior, ej. 14.2 Rel1).
- **CMake** (v3.13 o superior) y **Ninja** (recomendado para máxima velocidad).
- *Opcional:* Visual Studio Code con la extensión oficial **Raspberry Pi Pico**.

### 2. Compilación desde la Terminal (CLI)

Navega a la carpeta del firmware que deseas compilar (por ejemplo `01_pico_ecg`):

```powershell
# Entrar al firmware deseado
cd firmware/01_pico_ecg

# Generar los archivos de compilación con Ninja
cmake -G Ninja -B build -S .

# Compilar todo el proyecto (incluyendo automáticamente las librerías de common)
ninja -C build
```

> Si no tienes Ninja instalado, puedes usar el generador por defecto de CMake ejecutando:
> ```powershell
> cmake -B build -S .
> cmake --build build
> ```

### 3. Compilación desde Visual Studio Code

1. Abre la carpeta del firmware específico (por ejemplo `firmware/01_pico_ecg`) en VS Code.
2. Si tienes la extensión oficial de **Raspberry Pi Pico**, detectará automáticamente el archivo `.vscode/settings.json` con las rutas a la toolchain y el SDK.
3. Presiona **`Ctrl + Shift + B`** o haz clic en **Compile Project** en la barra inferior.

### 4. Carga en la Placa (Flasheo)

Una vez completada la compilación, en la carpeta `build/` se generará el archivo con extensión **`.uf2`** (ejemplo: `pico_ecg.uf2`):

1. Conecta la Raspberry Pi Pico / Pico 2 a la PC manteniendo presionado el pulsador **BOOTSEL**.
2. Aparecerá una unidad de almacenamiento masivo USB llamada **`RPI-RP2`** (o **`RP2350`** si usas Pico 2).
3. Arrastra o copia el archivo `.uf2` dentro de dicha unidad.
4. El microcontrolador se reiniciará automáticamente ejecutando el firmware.
