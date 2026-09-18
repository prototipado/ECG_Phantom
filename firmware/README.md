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

## 🗂️ Módulo Inicial: [`01_pico_ecg`](./01_pico_ecg/)

Plataforma de reproducción de señales clínicas reales almacenadas en memoria Flash:
- **Salida multicanal:** 9 potenciales analógicos de electrodos (**RA, LA, LL, V1–V6**) generados vía moduladores PWM a 100 kHz.
- **Interfaz local:** Menú gráfico en pantalla OLED (SSD1306/SH1106 vía SPI) administrado con encoder rotativo mediante la librería **GEM** y **u8g2**.
- **Consola interactiva CLI USB:** Comandos serie (`> set`, `> get`, `> list`, `> play`, `> stop`, `> stream on/off`, `> ver`).
- **Streaming de alta velocidad:** Transmisión binaria IEEE 754 a 500 SPS compatible con `software/ecg_gui.py`.
- **Feedback audiovisual:** LED indicador y buzzer piezoeléctrico sincronizados con la onda R (complejo QRS).
- *Documentación detallada, pinout completo y comandos:* Consultar [firmware/01_pico_ecg/README.md](./01_pico_ecg/README.md).

---

## 🗂️ Componentes Compartidos

La carpeta **`common/`** contiene librerías compartidas entre los distintos firmwares:
- Drivers para pantalla OLED (SSD1306/SH1106) mediante I2C o SPI.
- Control por Encoder Rotativo con pulsador.
- Rutinas de comunicación SPI para DAC externo y PWM de alta resolución.
- Protocolos de streaming serie USB hacia el software host.

