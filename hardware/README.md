# Diseño de Hardware (KiCad)

Este directorio contiene el diseño electrónico y mecánico del Fantoma ECG, organizado como un proyecto unificado en KiCad.

## Estructura del proyecto

- **`pico_ecg.kicad_pro`**: Archivo principal del proyecto KiCad.
- **`pico_ecg.kicad_sch`**: Esquema de circuito completo (etapa analógica, microcontrolador y conectores).
- **`pico_ecg.kicad_pcb`**: Ruteo de la placa de circuito impreso (PCB).
- **`pico_ecg.pretty/`**: Librería de huellas (footprints) locales utilizadas en el diseño.
- **`3d_models/`**: Modelos 3D de componentes (STEP/WRL) y partes de gabinete para impresión 3D.
- **`gerber/`**: Archivos de fabricación (Gerber) y de perforación (Drill).
- **`bom/`**: Lista de materiales (Bill of Materials) con referencias de componentes.
- **`images/`**: Renderizados y fotografías del hardware ensamblado.
- **`docs/`**: Diagramas de pines y notas de diseño.
