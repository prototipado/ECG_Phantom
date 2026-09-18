# 🔌 Diseño de Hardware (KiCad)

Este directorio contiene el diseño electrónico y mecánico del Fantoma ECG, organizado como un proyecto unificado de KiCad (`pico_ecg`).

## 📁 Estructura del Proyecto de Hardware

- **`pico_ecg.kicad_pro`**: Archivo de proyecto principal de KiCad.
- **`pico_ecg.kicad_sch`**: Esquema de circuito completo (etapa analógica, microcontrolador y conectores).
- **`pico_ecg.kicad_pcb`**: Ruteo y diseño de la placa de circuito impreso.
- **`pico_ecg.pretty/`**: Librería de footprints/huellas específicas utilizadas en el diseño.
- **`3d_models/`**: Modelos 3D de componentes (STEP/WRL) y carcasas/gabinetes para impresión 3D (STL/STEP).
- **`gerber/`**: Archivos de fabricación Gerber y taladros (Drill) para manufactura de la PCB.
- **`bom/`**: Lista de materiales (Bill of Materials) con referencias de componentes y proveedores.
- **`images/`**: Renderizados 3D, capturas del ruteo y fotos del montaje.
- **`docs/`**: Pinouts, especificaciones de capas y notas de diseño electrónico.
