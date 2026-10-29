"""Genera capturas sintéticas "tipo osciloscopio" (Tektronix TDS1012C) de cada
derivación del modelo de conducción cardíaca con los parámetros por defecto del
firmware (72 bpm, amplitud 100 %, HRV y baseline wander activos).

Replica Drivers/src/cardiac_model.c (RK4 a 2 kHz) y la cadena de salida de
signal_gen.c: la autocalibración escala el rango (min..max de todas las
derivaciones) a ADC 300..3100 sobre 3.3 V, es decir, 2.256 Vpp en el pin.

Salida: imagenes_sinteticas/<lead>.png (960x900 px por defecto; --scale 1 da 320x300 como los BMP medidos),
        imagenes_sinteticas/montaje.png y imagenes_sinteticas/senales.csv

Uso:  py simular_capturas.py [--bpm 72] [--vpp 2.256] [--tdiv 0.05] [--vdiv 0.2]
"""
import argparse
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

FS = 2000.0
DT = 1.0 / FS
LEADS = ["LA", "LL", "V1", "V2", "V3", "V4", "V5", "V6"]
LEAD_GAIN = [
    (0.55, 0.50, 0.75, 0.55), (1.00, 0.70, 1.00, 1.00),
    (0.30, 0.40, -0.45, 0.30), (0.40, 0.40, 0.60, 0.50),
    (0.50, 0.40, 1.00, 0.70), (0.60, 0.40, 1.20, 0.80),
    (0.60, 0.40, 1.00, 0.90), (0.60, 0.40, 0.80, 0.90),
]
P = dict(a1=40.0, a2=50.0, a3=50.0, f2=8.4, f3=1.5, u=0.69, d=3.0,
         e1=3.5, e2=5.0, e3=12.0, tau_ms=4.0, hrv_depth=0.05,
         bl_amp=0.15, bl_f=0.1)
K = np.array([2e3, 1e2, 1e4, 2e3])
C = np.array([0.26, 0.12, 0.12, 0.1])
B = np.array([0.0, 0.0, 0.015, 0.0])
G = np.array([0.4, 0.09, 0.09, 0.1])
H = np.array([0.004, 0.008, 0.008, 0.008])
W1 = np.array([0.13, 0.12, 0.12, 0.22])
W2 = np.array([1.0, 1.1, 1.1, 0.8])
CC = np.array([4e-5, -4e-5, 9e-5, 6e-5])


def deriv(s, dy1, dy2, f1):
    x1, y1, x2, y2, x3, y3 = s[:6]
    z, v = s[6:14:2], s[7:14:2]
    d = np.zeros(14)
    d[0] = y1
    d[1] = -P["a1"] * (x1 * x1 - P["u"]) * y1 - f1 * x1 * (x1 + P["d"]) * (x1 + P["e1"])
    d[2] = y2
    d[3] = (-P["a2"] * (x2 * x2 - P["u"]) * y2
            - P["f2"] * x2 * (x2 + P["d"]) * (x2 + P["e2"]) + f1 * (dy1 - y2))
    d[4] = y3
    d[5] = (-P["a3"] * (x3 * x3 - P["u"]) * y3
            - P["f3"] * x3 * (x3 + P["d"]) * (x3 + P["e3"]) + f1 * (dy2 - y3))
    Y = np.array([y1, -y1, y3, -y3])
    I = CC * Y * (Y > 0)
    dz = K * (-C * z * (z - W1) * (z - W2) - B * v - G * v * z + I)
    dv = K * H * (z - v)
    d[6:14:2] = dz
    d[7:14:2] = dv
    return d


def simulate(bpm, seconds):
    f = bpm / 60.0
    f1_base = max(1.0, -0.7319 + 8.798 * f + 9.098 * f * f)
    s = np.zeros(14)
    s[[0, 2, 4]] = -0.1
    s[[1, 3, 5]] = 0.025
    n_hist = 256
    h1 = np.zeros(n_hist)
    h2 = np.zeros(n_hist)
    idx = 0
    delay = min(int(P["tau_ms"] * 1e-3 / DT), n_hist - 1)
    t = 0.0
    n = int(seconds * FS)
    out = np.zeros((n, 8))
    gain = np.array(LEAD_GAIN)
    for i in range(n):
        t += DT
        rsa = math.sin(2 * math.pi * 0.25 * t)
        may = math.sin(2 * math.pi * 0.1 * t)
        f1 = max(0.5, f1_base * (1 + P["hrv_depth"] * (0.6 * rsa + 0.4 * may)))
        r = (idx + n_hist - delay) % n_hist
        dy1, dy2 = h1[r], h2[r]
        k1 = deriv(s, dy1, dy2, f1)
        k2 = deriv(s + DT * 0.5 * k1, dy1, dy2, f1)
        k3 = deriv(s + DT * 0.5 * k2, dy1, dy2, f1)
        k4 = deriv(s + DT * k3, dy1, dy2, f1)
        s = s + DT / 6 * (k1 + 2 * k2 + 2 * k3 + k4)
        h1[idx], h2[idx] = s[1], s[3]
        idx = (idx + 1) % n_hist
        base = P["bl_amp"] * math.sin(2 * math.pi * P["bl_f"] * t)
        z1, z2, z3, z4 = s[6], s[8], s[10], s[12]
        out[i] = base + gain[:, 0] * z1 - gain[:, 1] * z2 + gain[:, 2] * z3 + gain[:, 3] * z4
    return out


def render(ax_fig, trace_v, t, name, trig_t, tdiv, vdiv, base_div):
    fig = ax_fig
    ax = fig.add_axes([0.03, 0.22, 0.64, 0.64])
    ax.set_xlim(0, 10)
    ax.set_ylim(-4, 4)
    ax.set_xticks(range(0, 11))
    ax.set_yticks(range(-4, 5))
    ax.grid(True, color="#bbbbbb", lw=0.5, ls=":")
    ax.tick_params(labelleft=False, labelbottom=False, length=0)
    for sp in ax.spines.values():
        sp.set_color("#666666")
    x = (t - (trig_t - 3 * tdiv)) / tdiv
    ax.plot(x, base_div + trace_v / vdiv, color="#f5a31a", lw=1.0)
    ax.annotate("1", (0, base_div), xytext=(-9, 0), textcoords="offset points",
                fontsize=6, va="center")
    ax.plot([3], [4.0], marker="v", color="k", ms=3, clip_on=False)
    fig.text(0.04, 0.93, "SINTÉTICO", fontsize=7, weight="bold")
    fig.text(0.50, 0.93, f"M Pos: {3 * tdiv * 1e3:.1f}ms", fontsize=6)
    fig.text(0.04, 0.15, f"CH1 {vdiv * 1e3:.0f}mV", fontsize=6)
    fig.text(0.36, 0.15, f"M {tdiv * 1e3:.1f}ms", fontsize=6)
    fig.text(0.70, 0.87, "Fuente  CH1\nPendiente  Positiva\nAcoplamiento  CC",
             fontsize=5.5, va="top")
    fig.text(0.5, 0.04, f"Derivación {name}", fontsize=8, ha="center")


def comparar(outdir):
    """Matriz 4x4: cada par de columnas es (real, simulada) de una derivación."""
    from PIL import Image
    real_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "imagenes")
    fig, axs = plt.subplots(4, 4, figsize=(16, 14))
    for i, name in enumerate(LEADS):
        r, c = divmod(i, 2)
        real = os.path.join(real_dir, name.lower() + ".bmp")
        sim = os.path.join(outdir, name.lower() + ".png")
        for k, (path, tag) in enumerate([(real, "real"), (sim, "simulada")]):
            ax = axs[r, 2 * c + k]
            ax.imshow(Image.open(path).convert("RGB"))
            ax.set_title(f"{name} {tag}", fontsize=10)
            ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "comparacion.png"), dpi=150)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bpm", type=float, default=72.0)
    ap.add_argument("--vpp", type=float, default=2.256,
                    help="excursión total tras la autocalibración (V en el pin)")
    ap.add_argument("--tdiv", type=float, default=0.05)
    ap.add_argument("--vdiv", type=float, default=0.2)
    ap.add_argument("--base-div", type=float, default=-2.0)
    ap.add_argument("--v1-base-div", type=float, default=0.0,
                    help="posición de la línea base de V1 (V1 es negativa; se sube a mitad de pantalla)")
    ap.add_argument("--scale", type=int, default=3,
                    help="factor de resolución (1 = 320x300 como el osciloscopio)")
    ap.add_argument("--outdir", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__))))
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)

    warm, seconds = 2.0, 12.0
    sig = simulate(a.bpm, warm + seconds)
    sig = sig[int(warm * FS):]
    t = np.arange(len(sig)) * DT
    # Autocalibración: rango global de todas las derivaciones -> vpp
    volts = (sig - (sig.min() + sig.max()) / 2) * (a.vpp / (sig.max() - sig.min()))

    # Disparo común: flanco positivo de LL (derivación II) al 40 % de su rango
    ll = volts[:, 1]
    thr = ll.min() + 0.4 * (ll.max() - ll.min())
    cross = np.where((ll[:-1] < thr) & (ll[1:] >= thr))[0]
    cross = cross[(t[cross] > 0.5) & (t[cross] < seconds - 1.0)]
    trig_t = t[cross[1]] if len(cross) > 1 else t[cross[0]]
    win = (t >= trig_t - 0.5) & (t <= trig_t + 0.5)

    montage = plt.figure(figsize=(3.2 * 4, 3.0 * 2), dpi=100)
    for i, name in enumerate(LEADS):
        tr = volts[:, i]
        # Acople CC: se resta el valor medio de un periodo para centrar la línea base
        period = int(FS * 60.0 / a.bpm)
        tr = tr - tr[int(trig_t * FS) - period:int(trig_t * FS)].mean()
        fig = plt.figure(figsize=(3.2, 3.0), dpi=100 * a.scale)
        render(fig, tr, t, name, trig_t, a.tdiv, a.vdiv,
               a.v1_base_div if name == "V1" else a.base_div)
        fig.savefig(os.path.join(a.outdir, f"{name.lower()}.png"))
        plt.close(fig)

        ax = montage.add_subplot(2, 4, i + 1)
        ax.plot((t[win] - trig_t) * 1e3, tr[win], color="#f5a31a")
        ax.set_title(name)
        ax.grid(True, ls=":")
        ax.set_xlabel("ms (0 = disparo)")
        ax.set_ylabel("V (CA)")
    montage.tight_layout()
    montage.savefig(os.path.join(a.outdir, "montaje.png"), dpi=100 * a.scale)

    np.savetxt(os.path.join(a.outdir, "senales.csv"),
               np.column_stack([t[win] - trig_t, volts[win]]), delimiter=",",
               header="t_s," + ",".join(LEADS), comments="", fmt="%.6f")
    comparar(a.outdir)
    print("Generado en", a.outdir)


if __name__ == "__main__":
    main()

