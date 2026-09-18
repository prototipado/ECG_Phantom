import re
from pathlib import Path

# Configuración de archivos y parámetros
DIR_ACTUAL = Path(__file__).resolve().parent
REPO_ROOT = DIR_ACTUAL.parent.parent

# Buscar archivo de entrada en varias ubicaciones posibles
CANDIDATE_INPUTS = [
    DIR_ACTUAL / "Sinus_rhythm_N_9.h",
    REPO_ROOT / "firmware" / "01_pico_ecg" / "Signals" / "Sinus_rhythm_N_9.h",
    REPO_ROOT / "datasets" / "header_arrays" / "Sinus_rhythm_N_9.h",
]
ARCHIVO_ENTRADA = next((p for p in CANDIDATE_INPUTS if p.exists()), CANDIDATE_INPUTS[1])
ARCHIVO_SALIDA = DIR_ACTUAL.parent / "ltspice" / "ecg_pwl.txt"


TS = 0.002           # Tiempo de muestreo (2 ms)
VDD = 3.3            # Voltaje de alimentación del PWM en la Pico
MAX_PWM = 65535      # Resolución del PWM a 16 bits
COLUMNA = 0          # Derivación a extraer (índice 0 a 8)

def generar_pwl():
    if not ARCHIVO_ENTRADA.exists():
        print(f"Error: No existe el archivo {ARCHIVO_ENTRADA.name}")
        return

    with open(ARCHIVO_ENTRADA, "r", encoding="utf-8") as f:
        contenido = f.read()

    # Busca únicamente los bloques { ... } que contengan solo números y comas (ignora la estructura final)
    filas_raw = re.findall(r'\{([\d\s,+-]+)\}', contenido)
    
    vector_datos = []
    for fila in filas_raw:
        # Extrae todos los enteros de la fila
        numeros = [int(n) for n in re.findall(r'[-+]?\d+', fila)]
        
        # Filtra únicamente las filas que tengan las 9 columnas correspondientes
        if len(numeros) == 9:
            vector_datos.append(numeros[COLUMNA])

    if not vector_datos:
        print("Error: No se pudo extraer la matriz de datos.")
        return

    # Guarda el vector en formato Tiempo Voltaje para la fuente PWL de LTspice
    # Normalización para aprovechar todo el rango dinámico del PWM (0 a 3.3V)
    v_min = min(vector_datos)
    v_max = max(vector_datos)

    with open(ARCHIVO_SALIDA, "w") as f:
        for i, val in enumerate(vector_datos):
            tiempo = i * TS
            # Escala el valor entre 0 y 65535
            val_escalado = (val - v_min) / (v_max - v_min) * MAX_PWM
            voltaje = (val_escalado / MAX_PWM) * VDD
            f.write(f"{tiempo:.6f}\t{voltaje:.6f}\n")

    print(f"Éxito: Se procesaron {len(vector_datos)} muestras de {ARCHIVO_ENTRADA.name}.")
    print(f"Archivo PWL guardado en: {ARCHIVO_SALIDA}")

if __name__ == "__main__":
    generar_pwl()