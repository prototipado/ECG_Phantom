# 🫀 Fantoma ECG — Simulador y Calibrador Electrocardíaco Multicanal

[![Status](https://img.shields.io/badge/Status-In%20Development-yellow.svg)](#)
[![Hardware](https://img.shields.io/badge/Hardware-Raspberry%20Pi%20Pico%20%2F%20Pico%202-blue.svg)](#)
[![Python](https://img.shields.io/badge/Software-Python%203.9%2B-green.svg)](#)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](#)

Ecosistema abierto para el diseño, simulación, generación de bioseñales electrocardíacas y monitoreo en tiempo real. Este repositorio integra desde el diseño de hardware analógico/digital y simulaciones SPICE, hasta múltiples implementaciones de firmware para microcontroladores **RP2040 / RP2350** y una suite de software de escritorio en **Python**.

---

## 🧭 Visión General de la Arquitectura

```mermaid
flowchart TD
    subgraph HW["🔌 Hardware & Señal"]
        MCU["RP2040 / RP2350<br/>(Raspberry Pi Pico)"]
        FILT["Filtros Reconstrucción PWM/DAC<br/>(Simulados en LTSpice)"]
        OUT["Salida 9-12 Derivaciones ECG + PPG"]
        MCU --> FILT --> OUT
    end
    subgraph FW["🧠 Firmwares Sucesivos"]
        FW1["01. Reproductor Clínico (Flash/PhysioNet)"]
        FW2["02. Modelo Conducción (VdP + FHN)"]
        FW3["03. Síntesis Gaussiana + PPG"]
        FW4["04. Multi-Modelo 4-en-1"]
        FW5["05. Simulador Marcapasos (Pacer)"]
    end
    FW -.->|Carga según aplicación| MCU
    subgraph SW["💻 Software de Control Host"]
        PY["ecg_gui.py (Python)"]
        PLOT["Monitoreo multicanal en tiempo real"]
        CFG["Configuración de parámetros y patologías"]
        PY --> PLOT
        PY --> CFG
    end
    MCU <== "Streaming Serie USB (Handshake binario / 2000 SPS)" ==> PY
```

---

## 📁 Estructura del Proyecto

* **[`/hardware`](./hardware/)**: Proyecto completo KiCad (`pico_ecg`), esquemático (`.kicad_sch`), diseño de PCB (`.kicad_pcb`), librerías de huellas (`.pretty`), modelos 3D, gerbers y lista de materiales (BOM).
* **[`/simulaciones`](./simulaciones/)**: Modelos en LTSpice de las etapas de filtrado analógico (PWM a tensión analógica continua), detección de espigas y scripts de análisis en Python.
* **[`/firmware`](./firmware/)**: Módulos de firmware para Raspberry Pi Pico / Pico 2:
  * [`01_pico_ecg`](./firmware/01_pico_ecg/) *(Disponible)*: Reproductor de bases de datos clínicas en Flash SPI y ondas patrón.
  * *(Próximamente)*: `02_pico_conduction_model`, `03_pico_gaussian_ppg`, `04_pico_multimodel` y `05_pace_sim` (ver detalle en [`firmware/README.md`](./firmware/README.md)).
* **[`/software`](./software/)**: Aplicación de escritorio desarrollada en Python para control remoto, visualización multicanal en vivo y registro de datos.
* **[`/datasets`](./datasets/)**: Registros clínicos (PhysioNet, MIT-BIH, Lobachevsky) y herramientas de conversión a tablas de memoria C (`.h`).
* **[`/docs`](./docs/)**: Especificación del protocolo de comunicación USB, artículos científicos de referencia y notas de diseño.

---

## ⚡ Características Principales

- **Salida multicanal:** Emulación de derivaciones estándar (I, II, III, aVR, aVL, aVF, V1-V6) y canal PPG auxiliar.
- **Doble modo de operación:** Generación analógica real (salida a electrodos) y streaming serie binario de alta velocidad (hasta 2000 SPS).
- **Control interactivo:** Compatible con pantalla OLED SPI y rotary encoder local, o control total desde la interfaz gráfica de PC.
- **Modelado patológico:** Simulación de taquicardias, bradicardias, fibrilaciones, bloqueos AV, arritmia sinusal respiratoria (RSA) y derivas de línea base.

---

## 🚀 Inicio Rápido

### 1. Requisitos de Software (Python)
Para ejecutar la interfaz de control y monitoreo:

```bash
cd software
python -m venv .venv
# En Windows:
.venv\Scripts\activate
# En Linux/Mac:
source .venv/bin/activate
pip install -r requirements.txt
python ecg_gui.py
```

### 2. Grabación de Firmware
1. Entra en la carpeta del firmware disponible ([`firmware/01_pico_ecg/`](./firmware/01_pico_ecg/)).
2. Conecta la Raspberry Pi Pico manteniendo presionado el botón **BOOTSEL**.
3. Copia el archivo `.uf2` generado al disco de la Pico.

---

## 📝 Hoja de Ruta (Roadmap)

- [x] Caracterización de filtros pasabajos en LTSpice.
- [x] Integración de firmware inicial `01_pico_ecg` (reproductor clínico Flash/RAM).
- [ ] Subida sucesiva de firmwares modulares (`02_pico_conduction_model` a `05_pace_sim`).
- [x] Liberación de esquemáticos finales de PCB y circuito de adaptación.
- [x] Consolidación de la GUI en Python (v1.2.0) con streaming serie USB a 2000 SPS, registro CSV y script de compilación a ejecutable Windows.


---

## 📄 Licencia

Distribuido bajo la Licencia MIT. Consulta `LICENSE` para más información.
