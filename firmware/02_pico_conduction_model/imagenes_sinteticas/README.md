# Capturas sintéticas de osciloscopio

`simular_capturas.py` simula las derivaciones (LA, LL, V1–V6) del modelo por defecto del firmware
(72 bpm, HRV 0.05, baseline wander 0.15 mV a 0.1 Hz) y las dibuja con el aspecto de las capturas
reales del Tektronix TDS1012C-EDU de `../imagenes/`, para poder compararlas.

## Uso

```powershell
cd firmware\02_pico_conduction_model\imagenes_sinteticas
py simular_capturas.py
```

Requiere `numpy` y `matplotlib`.

| Opción | Defecto | Descripción |
|---|---|---|
| `--bpm` | 72 | Frecuencia cardíaca |
| `--vpp` | 2.256 | Excursión total (V en el pin) tras la autocalibración |
| `--tdiv` / `--vdiv` | 0.05 / 0.2 | s/div y V/div |
| `--base-div` | -2 | Posición de la línea base (divisiones desde el centro) |
| `--v1-base-div` | 0 | Línea base de V1 (negativa, se sube a mitad de pantalla) |
| `--scale` | 3 | Resolución: 1 = 320x300 (como los BMP), 3 = 960x900 |
| `--outdir` | esta carpeta | Destino |

## Salidas

- `la.png, ll.png, v1.png … v6.png`: una captura por derivación.
- `montaje.png`: las 8 derivaciones en una sola figura (eje en voltios y ms).
- `comparacion.png`: matriz 4x4 con cada derivación real (`../imagenes/*.bmp`) junto a su simulada.
- `senales.csv`: señales en V (acople CC) alrededor del disparo, para superponer con mediciones.

## Cómo funciona

1. **Modelo**: porta a NumPy el RK4 de `Drivers/src/cardiac_model.c` a 2 kHz (mismos parámetros,
   delay y tabla `LEAD_GAIN`). Se descartan 2 s de calentamiento y se usan 12 s.
2. **Escala**: el firmware autocalibra para que el mínimo y máximo globales de todas las derivaciones
   ocupen ADC 300..3100 (3.3 V). Se escala el rango global a `--vpp`. La escala real depende de la
   etapa de OpAmps; ajustar `--vpp` hasta que coincida con lo medido (p. ej. pico R de V4 ≈ 1.08 V).
3. **Disparo**: flanco positivo de LL al 40 % de su rango, ubicado a 3 divisiones del borde izquierdo.
4. **Acople CC**: se resta la media del periodo previo al disparo.
5. **Render**: 10x8 divisiones, traza naranja, CH1 en la línea base `--base-div`.

## Limitaciones

- La tabla `LEAD_GAIN` es heurística, así que las amplitudes relativas entre derivaciones son aproximadas.
- Se asume ganancia unitaria del filtro de salida y no se modela ruido ni ancho de banda del osciloscopio.
- El texto "M Pos" es el desplazamiento calculado, no el valor que muestra el equipo.
