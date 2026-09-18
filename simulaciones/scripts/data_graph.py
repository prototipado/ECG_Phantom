import matplotlib

# Evita el cuelgue de Qt en Anaconda/VS Code (debe ir antes de importar pyplot)
matplotlib.use("TkAgg")

from pathlib import Path
import matplotlib.pyplot as plt
import pandas as pd

DIR_ACTUAL = Path(__file__).parent
ARCHIVO_ENTRADA = DIR_ACTUAL / "filter_pwm_out_signals.txt"

# 1. Cargar datos rápidamente especificando tabulación y motor C
df = pd.read_csv(ARCHIVO_ENTRADA, sep="\t", engine="c")
df.columns = df.columns.str.strip()

# 2. Submuestreo automático si la simulación tiene demasiados puntos (acelera el renderizado)
MAX_PUNTOS = 50000
if len(df) > MAX_PUNTOS:
    paso = len(df) // MAX_PUNTOS
    df_plot = df.iloc[::paso].copy()
else:
    df_plot = df.copy()

col_tiempo = df_plot.columns[0]
cols_voltaje = df_plot.columns[1:]
tiempo = df_plot[col_tiempo]

# --- FIGURA 1: Gráficas separadas ---
fig1, axes = plt.subplots(
    len(cols_voltaje), 1, figsize=(10, 7), sharex=True, dpi=90
)
colores = ["#0072BD", "#D9531F", "#7E2EE8", "#218838"]

for i, col in enumerate(cols_voltaje):
    axes[i].plot(tiempo, df_plot[col], color=colores[i % len(colores)], linewidth=1)
    axes[i].set_ylabel(col, fontsize=9)
    axes[i].grid(True, linestyle="--", alpha=0.5)

axes[-1].set_xlabel("Tiempo (s)", fontsize=10)
fig1.suptitle("Análisis individual de señales (LTspice)", fontsize=12)
fig1.tight_layout()

# --- FIGURA 2: Superposición Original vs Filtrada ---
fig2, ax2 = plt.subplots(figsize=(10, 5), dpi=90)
if "V(n002)" in df_plot.columns:
    ax2.plot(
        tiempo,
        df_plot["V(n002)"],
        label="Señal Filtrada V(n002)",
        color="#0072BD",
        alpha=.5,
        linewidth=.5,
    )
if "V(v_sig)" in df_plot.columns:
    ax2.plot(
        tiempo,
        df_plot["V(v_sig)"],
        label="Señal Original V(v_sig)",
        color="#D9531F",
        alpha=1,
        linewidth=1,
    )



ax2.set_title("Comparación: Señal Original vs. Filtrada")
ax2.set_xlabel("Tiempo (s)")
ax2.set_ylabel("Voltaje (V)")
ax2.legend(loc="upper right")
ax2.grid(True, linestyle="--", alpha=0.5)

plt.show()