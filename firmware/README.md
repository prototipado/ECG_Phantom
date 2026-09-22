# Firmwares de Emulación Cardíaca (RP2040 / RP2350)

Este directorio alberga los módulos de firmware diseñados para microcontroladores Raspberry Pi Pico y Pico 2 (RP2040 / RP2350). Cada firmware aborda un método distinto de síntesis o reproducción de señales electrofisiológicas.

## Matriz de firmwares

| Carpeta | Nombre / Enfoque | Estado | Método de Generación | Salidas Soportadas | Complejidad Computacional |
|---|---|---|---|---|---|
| **`01_pico_ecg`** | Reproductor Clínico | Disponible | Lectura directa de Flash/RAM (PhysioNet) | 1 a 12 derivaciones | Baja (Look-up Table / DMA) |
| **`02_pico_conduction_model`** | Modelo Electrofisiológico | En desarrollo | Ecuaciones diferenciales acopladas (Van der Pol + FHN) | Derivaciones bipolares / derivaciones sintéticas | Media-Alta (Cálculo numérico RK4/Euler) |
| **`03_pico_gaussian_ppg`** | Síntesis Analítica + PPG | Planificado | Suma de curvas Gaussianas dinámicas | 9 derivaciones ECG + Pulso Óptico PPG | Media |
| **`04_pico_multimodel`** | Suite Multi-Motor 4-en-1 | Planificado | Selector de motor matemático en caliente | Multicanal configurable | Alta |
| **`05_pace_sim`** | Simulador de Marcapasos | Planificado | Inyección de espigas analógicas/digitales con tiempos configurables | Salida de estimulación cardíaca | Baja-Media |

## Módulo inicial: `01_pico_ecg`

Plataforma de reproducción de señales clínicas reales almacenadas en memoria Flash:
- **Salida multicanal:** 9 potenciales analógicos de electrodos (RA, LA, LL, V1–V6) generados vía moduladores PWM a 100 kHz.
- **Interfaz local:** Menú gráfico en pantalla OLED (SSD1306/SH1106 vía SPI) administrado con encoder rotativo mediante la librería GEM y u8g2.
- **Consola interactiva CLI USB:** Comandos serie (`> set`, `> get`, `> list`, `> play`, `> stop`, `> stream on/off`, `> ver`).
- **Streaming de alta velocidad:** Transmisión binaria IEEE 754 a 500 SPS compatible con `software/ecg_gui.py`.
- **Indicación auditiva y visual:** LED indicador y buzzer piezoeléctrico sincronizados con la onda R (complejo QRS).
- *Documentación detallada, pinout completo y comandos:* Consultar [firmware/01_pico_ecg/README.md](./01_pico_ecg/README.md).

## Componentes compartidos (`common/`)

Para evitar duplicar código, el directorio `common/` centraliza módulos compartidos configurados como librerías estáticas de CMake:

```text
firmware/
├── common/
│   ├── CMakeLists.txt     # Define los targets estáticos 'u8g2' y 'gem'
│   ├── GEM/               # Graphic Embedded Menu
│   └── u8g2/              # Librería gráfica u8g2/u8x8
├── 01_pico_ecg/           # Firmware de reproducción
└── 02_.../                # Otros firmwares
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

