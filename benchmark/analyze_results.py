#!/usr/bin/env python3
"""
TinyCFG Benchmark Result Analyzer — MSc thesis tables

Reads results_raw.csv from tinycfg_bench and writes BENCHMARK_ANALYSIS.md
with summary tables suitable for coursework / thesis.

Usage:
    python analyze_results.py results_raw.csv
    python analyze_results.py results_raw.csv --latex
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path
from typing import Any


def read_results(path: Path) -> tuple[dict[str, str], list[dict[str, Any]]]:
    meta: dict[str, str] = {}
    rows: list[dict[str, Any]] = []

    with path.open(newline="", encoding="utf-8") as f:
        for line in f:
            if line.startswith("# platform,"):
                meta["platform"] = line.split(",", 1)[1].strip()
            elif line.startswith("# iterations_per_case,"):
                meta["iterations"] = line.split(",", 1)[1].strip()
            elif line.startswith("# grammar,"):
                meta["grammar"] = line.split(",", 1)[1].strip()
            elif line.startswith("# sizeof_TinyCFG,"):
                meta["sizeof"] = line.split(",", 1)[1].strip()

        f.seek(0)
        reader = csv.DictReader(
            (ln for ln in f if not ln.startswith("#")),
            skipinitialspace=True,
        )
        for row in reader:
            if not row.get("id"):
                continue
            row["id"] = int(row["id"])
            row["expect_pass"] = int(row["expect_pass"])
            row["actual_pass"] = int(row["actual_pass"])
            row["action_count"] = int(row.get("action_count") or 0)
            for k in ("mean_us", "min_us", "max_us", "std_us", "p95_us", "confidence"):
                row[k] = float(row.get(k) or 0)
            for k in ("tokens", "ram_bytes", "phonetic_hits", "glued_segments", "noise_skipped"):
                row[k] = int(float(row.get(k) or 0))
            rows.append(row)

    return meta, rows


def accuracy(rows: list[dict]) -> float:
    if not rows:
        return 0.0
    ok = sum(1 for r in rows if r["actual_pass"] == r["expect_pass"])
    return 100.0 * ok / len(rows)


def md_table(headers: list[str], rows: list[list[str]]) -> str:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(str(c) for c in row) + " |")
    return "\n".join(lines)


def latex_table(headers: list[str], rows: list[list[str]], caption: str, label: str) -> str:
    colspec = "l" + "r" * (len(headers) - 1)
    lines = [
        r"\begin{table}[htbp]",
        r"\centering",
        rf"\caption{{{caption}}}",
        rf"\label{{{label}}}",
        rf"\begin{{tabular}}{{{colspec}}}",
        r"\toprule",
        " & ".join(headers) + r" \\",
        r"\midrule",
    ]
    for row in rows:
        lines.append(" & ".join(str(c) for c in row) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}"]
    return "\n".join(lines)


def build_report(meta: dict, rows: list[dict], latex: bool) -> str:
    now = datetime.now().strftime("%Y-%m-%d %H:%M")
    by_cat: dict[str, list[dict]] = defaultdict(list)
    for r in rows:
        by_cat[r["category"]].append(r)

    cat_order = [
        "single_clean", "multi_and", "multi_or", "multi_juxta", "named_slot",
        "stt_noise", "glued_dgs", "fuzzy_per", "temporal_then", "temporal_after",
        "cross_domain", "negative",
    ]
    ordered_cats = [c for c in cat_order if c in by_cat] + sorted(
        set(by_cat) - set(cat_order)
    )

    # Table 1 — Dataset overview
    t1_rows = []
    for cat in ordered_cats:
        items = by_cat[cat]
        pos = sum(1 for r in items if r["expect_pass"] == 1)
        neg = len(items) - pos
        t1_rows.append([cat.replace("_", " "), str(len(items)), str(pos), str(neg)])

    t1_rows.append([
        "**Total**",
        str(len(rows)),
        str(sum(1 for r in rows if r["expect_pass"] == 1)),
        str(sum(1 for r in rows if r["expect_pass"] == 0)),
    ])

    # Table 2 — Accuracy by category
    t2_rows = []
    for cat in ordered_cats:
        items = by_cat[cat]
        acc = accuracy(items)
        fails = [r for r in items if r["actual_pass"] != r["expect_pass"]]
        t2_rows.append([
            cat.replace("_", " "),
            str(len(items)),
            f"{acc:.1f}%",
            str(len(fails)),
        ])
    t2_rows.append(["**Overall**", str(len(rows)), f"**{accuracy(rows):.1f}%**", ""])

    # Table 3 — Latency by category (µs)
    t3_rows = []
    all_mean = [r["mean_us"] for r in rows]
    all_p95 = [r["p95_us"] for r in rows]
    for cat in ordered_cats:
        items = by_cat[cat]
        means = [r["mean_us"] for r in items]
        p95s = [r["p95_us"] for r in items]
        t3_rows.append([
            cat.replace("_", " "),
            f"{statistics.mean(means):.1f}",
            f"{min(r['min_us'] for r in items):.1f}",
            f"{max(r['max_us'] for r in items):.1f}",
            f"{statistics.mean(p95s):.1f}",
        ])
    t3_rows.append([
        "**Overall**",
        f"**{statistics.mean(all_mean):.1f}**",
        f"{min(r['min_us'] for r in rows):.1f}",
        f"{max(r['max_us'] for r in rows):.1f}",
        f"**{statistics.mean(all_p95):.1f}**",
    ])

    # Table 4 — Recovery & robustness metrics (positive cases only)
    pos = [r for r in rows if r["expect_pass"] == 1]
    t4_rows = []
    for cat in ordered_cats:
        items = [r for r in by_cat[cat] if r["expect_pass"] == 1]
        if not items:
            continue
        t4_rows.append([
            cat.replace("_", " "),
            f"{statistics.mean(r['confidence'] for r in items):.3f}",
            f"{statistics.mean(r['tokens'] for r in items):.1f}",
            str(sum(r["phonetic_hits"] for r in items)),
            str(sum(r["glued_segments"] for r in items)),
            str(sum(r["noise_skipped"] for r in items)),
        ])
    t4_rows.append([
        "**Positive cases**",
        f"**{statistics.mean(r['confidence'] for r in pos):.3f}**",
        f"{statistics.mean(r['tokens'] for r in pos):.1f}",
        str(sum(r["phonetic_hits"] for r in pos)),
        str(sum(r["glued_segments"] for r in pos)),
        str(sum(r["noise_skipped"] for r in pos)),
    ])

    # Table 5 — Failed cases detail
    fails = [r for r in rows if r["actual_pass"] != r["expect_pass"]]
    t5_rows = []
    for r in fails:
        t5_rows.append([
            str(r["id"]),
            r["category"],
            r["input"][:50] + ("…" if len(r["input"]) > 50 else ""),
            "PASS" if r["expect_pass"] else "FAIL",
            "PASS" if r["actual_pass"] else "FAIL",
        ])
    if not t5_rows:
        t5_rows.append(["—", "—", "No mismatches", "—", "—"])

    # Table 6 — System configuration
    t6_rows = [
        ["Grammar", meta.get("grammar", "smart_world v3")],
        ["Platform", meta.get("platform", "host-pc")],
        ["Iterations / case", meta.get("iterations", "500")],
        ["Parser instance size", f"{meta.get('sizeof', '?')} bytes"],
        ["Dataset size", str(len(rows))],
        ["Overall accuracy", f"{accuracy(rows):.1f}%"],
        ["Mean latency", f"{statistics.mean(all_mean):.1f} µs"],
        ["P95 latency", f"{statistics.mean(all_p95):.1f} µs"],
        ["Date generated", now],
    ]

    h1 = ["Category", "Cases", "Positive", "Negative"]
    h2 = ["Category", "Cases", "Accuracy", "Mismatches"]
    h3 = ["Category", "Mean µs", "Min µs", "Max µs", "P95 µs"]
    h4 = ["Category", "Mean conf.", "Mean tokens", "PER hits", "DGS segs", "Noise skip"]
    h5 = ["ID", "Category", "Input", "Expected", "Actual"]
    h6 = ["Parameter", "Value"]

    parts = [
        "# TinyCFG Benchmark Analysis",
        "",
        f"*Generated: {now}*",
        "",
        "## Experimental setup",
        "",
        md_table(h6, t6_rows),
        "",
        "## Table 1 — Dataset composition by category",
        "",
        md_table(h1, t1_rows),
        "",
        "## Table 2 — Parsing accuracy by category",
        "",
        md_table(h2, t2_rows),
        "",
        "## Table 3 — Parse latency by category (microseconds)",
        "",
        "Host-side timing wraps full `TinyCFG::parse()` including tokenization, "
        "pattern matching, CDA, and data-flow snapshot build.",
        "",
        md_table(h3, t3_rows),
        "",
        "## Table 4 — Robustness metrics (positive test cases)",
        "",
        md_table(h4, t4_rows),
        "",
        "## Table 5 — Misclassified / failed cases",
        "",
        md_table(h5, t5_rows),
        "",
        "## Notes for MSc report",
        "",
        "- **Latency** on host PC is for relative comparison across categories; "
        "ESP32 absolute values will differ (use `bench` on Serial CLI for on-device numbers).",
        "- **Categories** map to TinyCFG features: DGS (glued_dgs), PER/fuzzy (fuzzy_per), "
        "composition operators (multi_and / multi_or / multi_juxta).",
        "- **Negative cases** validate that the parser rejects out-of-grammar input.",
        "",
    ]

    if latex:
        parts += [
            "---",
            "",
            "## LaTeX tables (copy into thesis)",
            "",
            "```latex",
            latex_table(h2, t2_rows, "TinyCFG parsing accuracy by category", "tab:tcfg-acc"),
            "",
            latex_table(h3, t3_rows, "TinyCFG parse latency by category ($\\mu$s)", "tab:tcfg-lat"),
            "```",
            "",
        ]

    return "\n".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser(description="Analyze TinyCFG benchmark CSV")
    ap.add_argument("csv_file", nargs="?", default="results_raw.csv")
    ap.add_argument("-o", "--output", default="BENCHMARK_ANALYSIS.md")
    ap.add_argument("--latex", action="store_true", help="Include LaTeX table snippets")
    args = ap.parse_args()

    path = Path(args.csv_file)
    if not path.exists():
        print(f"ERROR: file not found: {path}", file=sys.stderr)
        return 1

    meta, rows = read_results(path)
    if not rows:
        print("ERROR: no data rows in CSV", file=sys.stderr)
        return 1

    report = build_report(meta, rows, args.latex)
    out = Path(args.output)
    out.write_text(report, encoding="utf-8")
    print(f"Analysis written to: {out}")
    print(f"  Cases: {len(rows)}  Accuracy: {accuracy(rows):.1f}%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
