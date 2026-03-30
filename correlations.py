#!/usr/bin/env python3
import os
import math
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

try:
    from scipy.stats import pearsonr, spearmanr
    SCIPY_AVAILABLE = True
except Exception:
    SCIPY_AVAILABLE = False


###############################################################################
# Configuración
###############################################################################

CSV_PATH = "/home/yo/jitterentropy-library/tests/raw-entropy/results-correlation-sweep/runtime_restart_entropy.csv"
OUT_DIR = "/home/yo/jitterentropy-library/tests/raw-entropy/results-correlation-sweep/analysis"

os.makedirs(OUT_DIR, exist_ok=True)

SCATTER_HC_PNG = os.path.join(OUT_DIR, "scatter_runtime_vs_restart_hc.png")
SCATTER_HR_PNG = os.path.join(OUT_DIR, "scatter_runtime_vs_restart_hr.png")
HEATMAP_RUNTIME_PNG = os.path.join(OUT_DIR, "heatmap_runtime_entropy.png")
HEATMAP_HC_PNG = os.path.join(OUT_DIR, "heatmap_restart_hc.png")
HEATMAP_HR_PNG = os.path.join(OUT_DIR, "heatmap_restart_hr.png")
CORR_CSV = os.path.join(OUT_DIR, "correlation_summary.csv")


###############################################################################
# Utilidades
###############################################################################

def ensure_numeric(df: pd.DataFrame, cols):
    for c in cols:
        df[c] = pd.to_numeric(df[c], errors="coerce")
    return df


def rankdata_simple(a: np.ndarray) -> np.ndarray:
    """
    Ranking simple con promedio para empates.
    Se usa solo si scipy no está disponible.
    """
    s = pd.Series(a)
    return s.rank(method="average").to_numpy()


def pearson_manual(x: np.ndarray, y: np.ndarray) -> float:
    if len(x) < 2:
        return np.nan
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    if np.std(x) == 0 or np.std(y) == 0:
        return np.nan
    return np.corrcoef(x, y)[0, 1]


def spearman_manual(x: np.ndarray, y: np.ndarray) -> float:
    if len(x) < 2:
        return np.nan
    rx = rankdata_simple(np.asarray(x, dtype=float))
    ry = rankdata_simple(np.asarray(y, dtype=float))
    return pearson_manual(rx, ry)


def compute_correlations(x: pd.Series, y: pd.Series):
    mask = x.notna() & y.notna()
    xv = x[mask].to_numpy(dtype=float)
    yv = y[mask].to_numpy(dtype=float)

    if len(xv) < 2:
        return {
            "n": len(xv),
            "pearson_r": np.nan,
            "pearson_p": np.nan,
            "spearman_rho": np.nan,
            "spearman_p": np.nan,
        }

    if SCIPY_AVAILABLE:
        try:
            pr, pp = pearsonr(xv, yv)
        except Exception:
            pr, pp = np.nan, np.nan
        try:
            sr, sp = spearmanr(xv, yv)
        except Exception:
            sr, sp = np.nan, np.nan
    else:
        pr = pearson_manual(xv, yv)
        sr = spearman_manual(xv, yv)
        pp = np.nan
        sp = np.nan

    return {
        "n": len(xv),
        "pearson_r": pr,
        "pearson_p": pp,
        "spearman_rho": sr,
        "spearman_p": sp,
    }


def add_point_labels(ax, df, xcol, ycol):
    for _, row in df.iterrows():
        if pd.notna(row[xcol]) and pd.notna(row[ycol]):
            label = f"osr={int(row['osr'])}, h={int(row['hloopcnt'])}"
            ax.annotate(
                label,
                (row[xcol], row[ycol]),
                textcoords="offset points",
                xytext=(5, 5),
                fontsize=8,
            )


def scatter_plot(df: pd.DataFrame, xcol: str, ycol: str, out_path: str, title: str):
    sub = df[[xcol, ycol, "osr", "hloopcnt"]].dropna().copy()

    fig, ax = plt.subplots(figsize=(8, 6))
    ax.scatter(sub[xcol], sub[ycol])

    add_point_labels(ax, sub, xcol, ycol)

    if len(sub) >= 2:
        x = sub[xcol].to_numpy(dtype=float)
        y = sub[ycol].to_numpy(dtype=float)

        if len(np.unique(x)) > 1:
            m, b = np.polyfit(x, y, 1)
            xs = np.linspace(np.min(x), np.max(x), 200)
            ys = m * xs + b
            ax.plot(xs, ys)

    corr = compute_correlations(sub[xcol], sub[ycol])

    text = (
        f"n = {corr['n']}\n"
        f"Pearson r = {corr['pearson_r']:.4f}\n"
        f"Spearman rho = {corr['spearman_rho']:.4f}"
    )

    ax.text(
        0.02,
        0.98,
        text,
        transform=ax.transAxes,
        va="top",
        ha="left",
        fontsize=10,
        bbox=dict(boxstyle="round", alpha=0.15),
    )

    ax.set_xlabel(xcol)
    ax.set_ylabel(ycol)
    ax.set_title(title)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(out_path, dpi=300, bbox_inches="tight")
    plt.close(fig)


def heatmap_plot(df: pd.DataFrame, value_col: str, out_path: str, title: str):
    """
    Heatmap con osr en filas y hloopcnt en columnas.
    """
    pivot = df.pivot_table(
        index="osr",
        columns="hloopcnt",
        values=value_col,
        aggfunc="mean"
    ).sort_index().sort_index(axis=1)

    fig, ax = plt.subplots(figsize=(7, 5))
    im = ax.imshow(pivot.to_numpy(dtype=float), aspect="auto")

    ax.set_xticks(range(len(pivot.columns)))
    ax.set_xticklabels(pivot.columns.tolist())
    ax.set_yticks(range(len(pivot.index)))
    ax.set_yticklabels(pivot.index.tolist())

    ax.set_xlabel("hloopcnt")
    ax.set_ylabel("osr")
    ax.set_title(title)

    for i in range(pivot.shape[0]):
        for j in range(pivot.shape[1]):
            val = pivot.iloc[i, j]
            txt = "NA" if pd.isna(val) else f"{val:.3f}"
            ax.text(j, i, txt, ha="center", va="center", fontsize=9)

    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label(value_col)

    plt.tight_layout()
    plt.savefig(out_path, dpi=300, bbox_inches="tight")
    plt.close(fig)


###############################################################################
# Main
###############################################################################

def main():
    df = pd.read_csv(CSV_PATH)

    numeric_cols = [
        "osr",
        "hloopcnt",
        "runtime_entropy",
        "restart_h_i",
        "restart_h_c",
        "restart_h_r",
        "restart_min_hirc",
    ]
    df = ensure_numeric(df, numeric_cols)

    # Ordenar para que tablas y etiquetas salgan limpias
    df = df.sort_values(["osr", "hloopcnt"]).reset_index(drop=True)

    # Mostrar datos cargados
    print("\nDatos cargados:")
    print(df[["osr", "hloopcnt", "runtime_entropy", "restart_h_i", "restart_h_c", "restart_h_r", "restart_min_hirc"]])

    # Correlaciones útiles
    corr_runtime_hc = compute_correlations(df["runtime_entropy"], df["restart_h_c"])
    corr_runtime_hr = compute_correlations(df["runtime_entropy"], df["restart_h_r"])
    corr_runtime_hi = compute_correlations(df["runtime_entropy"], df["restart_h_i"])
    corr_runtime_min = compute_correlations(df["runtime_entropy"], df["restart_min_hirc"])

    corr_df = pd.DataFrame([
        {
            "pair": "runtime_entropy vs restart_h_c",
            **corr_runtime_hc
        },
        {
            "pair": "runtime_entropy vs restart_h_r",
            **corr_runtime_hr
        },
        {
            "pair": "runtime_entropy vs restart_h_i",
            **corr_runtime_hi
        },
        {
            "pair": "runtime_entropy vs restart_min_hirc",
            **corr_runtime_min
        },
    ])

    corr_df.to_csv(CORR_CSV, index=False)

    print("\nResumen de correlaciones:")
    print(corr_df.to_string(index=False))

    # Scatter plots
    scatter_plot(
        df,
        xcol="runtime_entropy",
        ycol="restart_h_c",
        out_path=SCATTER_HC_PNG,
        title="Runtime entropy vs restart H_C",
    )

    scatter_plot(
        df,
        xcol="runtime_entropy",
        ycol="restart_h_r",
        out_path=SCATTER_HR_PNG,
        title="Runtime entropy vs restart H_R",
    )

    # Heatmaps
    heatmap_plot(
        df,
        value_col="runtime_entropy",
        out_path=HEATMAP_RUNTIME_PNG,
        title="Heatmap of runtime entropy",
    )

    heatmap_plot(
        df,
        value_col="restart_h_c",
        out_path=HEATMAP_HC_PNG,
        title="Heatmap of restart H_C",
    )

    heatmap_plot(
        df,
        value_col="restart_h_r",
        out_path=HEATMAP_HR_PNG,
        title="Heatmap of restart H_R",
    )

    # Matriz de correlación general entre variables numéricas interesantes
    corr_vars = [
        "runtime_entropy",
        "restart_h_i",
        "restart_h_c",
        "restart_h_r",
        "restart_min_hirc",
        "osr",
        "hloopcnt",
    ]
    corr_matrix = df[corr_vars].corr(method="spearman")
    corr_matrix_path = os.path.join(OUT_DIR, "correlation_matrix_spearman.csv")
    corr_matrix.to_csv(corr_matrix_path)

    print("\nMatriz de correlación Spearman:")
    print(corr_matrix)

    print("\nFicheros generados:")
    print(f"  {SCATTER_HC_PNG}")
    print(f"  {SCATTER_HR_PNG}")
    print(f"  {HEATMAP_RUNTIME_PNG}")
    print(f"  {HEATMAP_HC_PNG}")
    print(f"  {HEATMAP_HR_PNG}")
    print(f"  {CORR_CSV}")
    print(f"  {corr_matrix_path}")


if __name__ == "__main__":
    main()
