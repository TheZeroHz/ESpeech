#!/usr/bin/env python3
"""
Generate conference-paper figures from TinyCFG benchmark results.

Usage:
    python generate_graphs.py results/results_raw.csv -o results/figures
"""

from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

# Import shared CSV reader from analyze_results
from analyze_results import read_results, accuracy

CAT_ORDER = [
    "single_clean", "multi_and", "multi_or", "multi_juxta", "named_slot",
    "stt_noise", "glued_dgs", "fuzzy_per", "temporal_then", "cross_domain",
    "negative",
]

CAT_LABELS = {
    "single_clean": "Single",
    "multi_and": "AND",
    "multi_or": "OR",
    "multi_juxta": "Juxta",
    "named_slot": "Named",
    "stt_noise": "STT noise",
    "glued_dgs": "DGS glued",
    "fuzzy_per": "Fuzzy/PER",
    "temporal_then": "THEN",
    "cross_domain": "Cross-dom.",
    "negative": "Negative",
}

PALETTE = [
    "#2ecc71", "#3498db", "#9b59b6", "#1abc9c", "#f39c12",
    "#e74c3c", "#34495e", "#e67e22", "#16a085", "#2980b9", "#95a5a6",
]


def setup_style() -> None:
    plt.rcParams.update({
        "figure.dpi": 150,
        "savefig.dpi": 300,
        "font.family": "sans-serif",
        "font.size": 10,
        "axes.titlesize": 12,
        "axes.labelsize": 10,
        "legend.fontsize": 9,
        "figure.facecolor": "white",
    })


def ordered_categories(rows: list[dict]) -> list[str]:
    present = {r["category"] for r in rows}
    return [c for c in CAT_ORDER if c in present] + sorted(present - set(CAT_ORDER))


def fig_dataset_composition(rows: list[dict], out: Path) -> Path:
    cats = ordered_categories(rows)
    counts = [sum(1 for r in rows if r["category"] == c) for c in cats]
    labels = [CAT_LABELS.get(c, c) for c in cats]

    fig, ax = plt.subplots(figsize=(8, 5))
    wedges, texts, autotexts = ax.pie(
        counts, labels=labels, autopct="%1.1f%%", colors=PALETTE[: len(cats)],
        startangle=90, pctdistance=0.75,
    )
    for t in autotexts:
        t.set_fontsize(8)
    ax.set_title(f"Benchmark Dataset Composition (N={len(rows)})")
    path = out / "fig01_dataset_composition.png"
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return path


def fig_accuracy_by_category(rows: list[dict], out: Path) -> Path:
    cats = ordered_categories(rows)
    accs = []
    for cat in cats:
        items = [r for r in rows if r["category"] == cat]
        accs.append(100.0 * sum(1 for r in items if r["actual_pass"] == r["expect_pass"]) / len(items))

    fig, ax = plt.subplots(figsize=(10, 5))
    x = np.arange(len(cats))
    colors = ["#2ecc71" if a >= 90 else "#f39c12" if a >= 75 else "#e74c3c" for a in accs]
    bars = ax.bar(x, accs, color=colors, edgecolor="white", linewidth=0.5)
    ax.axhline(accuracy(rows), color="#2c3e50", linestyle="--", linewidth=1.2,
               label=f"Overall {accuracy(rows):.1f}%")
    ax.set_xticks(x)
    ax.set_xticklabels([CAT_LABELS.get(c, c) for c in cats], rotation=35, ha="right")
    ax.set_ylabel("Accuracy (%)")
    ax.set_ylim(0, 105)
    ax.set_title("Parsing Accuracy by Test Category")
    ax.legend(loc="lower right")
    for bar, val in zip(bars, accs):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 1,
                f"{val:.0f}%", ha="center", va="bottom", fontsize=8)
    path = out / "fig02_accuracy_by_category.png"
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return path


def fig_latency_by_category(rows: list[dict], out: Path) -> Path:
    cats = ordered_categories(rows)
    means = []
    p95s = []
    for cat in cats:
        items = [r for r in rows if r["category"] == cat]
        means.append(np.mean([r["mean_us"] for r in items]))
        p95s.append(np.mean([r["p95_us"] for r in items]))

    fig, ax = plt.subplots(figsize=(10, 5))
    x = np.arange(len(cats))
    w = 0.35
    ax.bar(x - w / 2, means, w, label="Mean", color="#3498db", edgecolor="white")
    ax.bar(x + w / 2, p95s, w, label="Mean P95", color="#2980b9", edgecolor="white")
    ax.set_xticks(x)
    ax.set_xticklabels([CAT_LABELS.get(c, c) for c in cats], rotation=35, ha="right")
    ax.set_ylabel("Latency (µs)")
    ax.set_title("Parse Latency by Category (Host PC)")
    ax.legend()
    path = out / "fig03_latency_by_category.png"
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return path


def fig_latency_distribution(rows: list[dict], out: Path) -> Path:
    all_mean = [r["mean_us"] for r in rows]
    fig, ax = plt.subplots(figsize=(8, 4.5))
    ax.hist(all_mean, bins=40, color="#3498db", edgecolor="white", alpha=0.85)
    ax.axvline(np.mean(all_mean), color="#e74c3c", linestyle="--", linewidth=1.5,
               label=f"Mean {np.mean(all_mean):.1f} µs")
    ax.set_xlabel("Mean parse latency per case (µs)")
    ax.set_ylabel("Frequency")
    ax.set_title(f"Latency Distribution (N={len(rows)} cases)")
    ax.legend()
    path = out / "fig04_latency_distribution.png"
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return path


def fig_confidence_by_category(rows: list[dict], out: Path) -> Path:
    cats = ordered_categories(rows)
    pos_cats = [c for c in cats if c != "negative"]
    confs = []
    for cat in pos_cats:
        items = [r for r in rows if r["category"] == cat and r["expect_pass"] == 1]
        confs.append(np.mean([r["confidence"] for r in items]) if items else 0)

    fig, ax = plt.subplots(figsize=(10, 5))
    x = np.arange(len(pos_cats))
    ax.bar(x, confs, color="#1abc9c", edgecolor="white")
    ax.set_xticks(x)
    ax.set_xticklabels([CAT_LABELS.get(c, c) for c in pos_cats], rotation=35, ha="right")
    ax.set_ylabel("Mean confidence score")
    ax.set_ylim(0, 1.05)
    ax.set_title("Parser Confidence by Category (positive cases)")
    path = out / "fig05_confidence_by_category.png"
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return path


def fig_robustness_metrics(rows: list[dict], out: Path) -> Path:
    pos = [r for r in rows if r["expect_pass"] == 1]
    metrics = {
        "PER hits": sum(r["phonetic_hits"] for r in pos),
        "DGS segments": sum(r["glued_segments"] for r in pos),
        "Noise skipped": sum(r["noise_skipped"] for r in pos),
    }
    fig, ax = plt.subplots(figsize=(6, 4))
    names = list(metrics.keys())
    vals = list(metrics.values())
    colors = ["#9b59b6", "#e67e22", "#e74c3c"]
    ax.bar(names, vals, color=colors, edgecolor="white")
    ax.set_ylabel("Total count (all positive cases)")
    ax.set_title("Recovery Mechanism Activations")
    for i, v in enumerate(vals):
        ax.text(i, v + max(vals) * 0.02, str(v), ha="center", fontsize=10)
    path = out / "fig06_robustness_activations.png"
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return path


def fig_confusion_summary(rows: list[dict], out: Path) -> Path:
    tp = sum(1 for r in rows if r["expect_pass"] == 1 and r["actual_pass"] == 1)
    fn = sum(1 for r in rows if r["expect_pass"] == 1 and r["actual_pass"] == 0)
    tn = sum(1 for r in rows if r["expect_pass"] == 0 and r["actual_pass"] == 0)
    fp = sum(1 for r in rows if r["expect_pass"] == 0 and r["actual_pass"] == 1)

    fig, ax = plt.subplots(figsize=(5, 4))
    data = np.array([[tp, fn], [fp, tn]])
    im = ax.imshow(data, cmap="Blues", vmin=0)
    ax.set_xticks([0, 1])
    ax.set_yticks([0, 1])
    ax.set_xticklabels(["Pred PASS", "Pred FAIL"])
    ax.set_yticklabels(["Label PASS", "Label FAIL"])
    labels = [["TP", "FN"], ["FP", "TN"]]
    for i in range(2):
        for j in range(2):
            ax.text(j, i, f"{labels[i][j]}\n{data[i, j]}", ha="center", va="center",
                    color="white" if data[i, j] > data.max() / 2 else "black", fontsize=12)
    ax.set_title("Parse Outcome Confusion Matrix")
    fig.colorbar(im, ax=ax, fraction=0.046)
    path = out / "fig07_confusion_matrix.png"
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return path


def fig_composition_operators(rows: list[dict], out: Path) -> Path:
    op_cats = ["multi_and", "multi_or", "multi_juxta", "temporal_then"]
    cats = [c for c in op_cats if any(r["category"] == c for r in rows)]
    accs = []
    for cat in cats:
        items = [r for r in rows if r["category"] == cat]
        accs.append(100.0 * sum(1 for r in items if r["actual_pass"] == r["expect_pass"]) / len(items))

    fig, ax = plt.subplots(figsize=(7, 4.5))
    labels = [CAT_LABELS.get(c, c) for c in cats]
    x = np.arange(len(cats))
    ax.bar(x, accs, color=["#3498db", "#9b59b6", "#1abc9c", "#f39c12"][: len(cats)])
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.set_ylabel("Accuracy (%)")
    ax.set_ylim(0, 105)
    ax.set_title("N-Intent Composition Operator Accuracy")
    for i, v in enumerate(accs):
        ax.text(i, v + 1, f"{v:.0f}%", ha="center", fontsize=10)
    path = out / "fig08_composition_operators.png"
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return path


def generate_all(csv_path: Path, out_dir: Path) -> list[Path]:
    setup_style()
    out_dir.mkdir(parents=True, exist_ok=True)
    meta, rows = read_results(csv_path)
    if not rows:
        raise SystemExit("No data in CSV")

    figures = [
        fig_dataset_composition(rows, out_dir),
        fig_accuracy_by_category(rows, out_dir),
        fig_latency_by_category(rows, out_dir),
        fig_latency_distribution(rows, out_dir),
        fig_confidence_by_category(rows, out_dir),
        fig_robustness_metrics(rows, out_dir),
        fig_confusion_summary(rows, out_dir),
        fig_composition_operators(rows, out_dir),
    ]
    return figures


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_file", nargs="?", default="results/results_raw.csv")
    ap.add_argument("-o", "--output", default="results/figures")
    args = ap.parse_args()

    csv_path = Path(args.csv_file)
    if not csv_path.exists():
        print(f"ERROR: {csv_path} not found", file=sys.stderr)
        return 1

    paths = generate_all(csv_path, Path(args.output))
    print(f"Generated {len(paths)} figures in {args.output}/")
    for p in paths:
        print(f"  {p.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
