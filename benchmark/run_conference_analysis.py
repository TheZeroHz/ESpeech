#!/usr/bin/env python3
"""
Full conference-paper benchmark pipeline:
  1. Generate dataset -> dataset/dataset.csv
  2. Run tinycfg_bench -> results/results_raw.csv
  3. Analyze tables -> results/BENCHMARK_ANALYSIS.md
  4. Generate figures -> results/figures/
  5. Assemble -> CONFERENCE_PAPER_RESULTS.md

Usage:
    python run_conference_analysis.py
    python run_conference_analysis.py --iterations 200 --cases 500
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATASET_DIR = ROOT / "dataset"
RESULTS_DIR = ROOT / "results"
FIGURES_DIR = RESULTS_DIR / "figures"


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print(f"\n>> {' '.join(cmd)}")
    subprocess.run(cmd, cwd=cwd or ROOT, check=True)


def find_bench_exe() -> Path:
    for name in ("tinycfg_bench.exe", "tinycfg_bench"):
        p = ROOT / name
        if p.exists():
            return p
    raise FileNotFoundError(
        "tinycfg_bench not found. Run: g++ ... or build.bat first"
    )


def build_conference_doc(meta: dict, analysis_path: Path, figures: list[str]) -> str:
    now = datetime.now().strftime("%Y-%m-%d %H:%M")
    grammar = meta.get("grammar", "smart_world v3")
    platform = meta.get("platform", "host-pc")
    iters = meta.get("iterations", "?")
    sizeof_parser = meta.get("sizeof", "?")

    fig_lines = "\n".join(
        f"![{Path(f).stem}](results/figures/{Path(f).name})\n\n"
        f"*Figure: {Path(f).stem.replace('_', ' ')}*\n"
        for f in figures
    )

    return f"""# TinyCFG — Conference Paper Results Package

*Generated: {now}*

This document consolidates benchmark results, tables, and figures for publication.
All artifacts are reproducible via `python run_conference_analysis.py`.

---

## 1. Abstract metrics (copy for paper)

| Metric | Value |
|--------|-------|
| Grammar | {grammar} |
| Test cases | 500 (11 categories, realistic STT noise) |
| Iterations / case | {iters} |
| Platform | {platform} |
| Parser memory | {sizeof_parser} bytes |
| Overall accuracy | See Table 2 in `results/BENCHMARK_ANALYSIS.md` |
| Mean parse latency | See Table 3 in `results/BENCHMARK_ANALYSIS.md` |

**Contributions evaluated:** Pattern-augmented LL(1) parser, DGS deglue, PER/fuzzy recovery, gap-tolerant matching, N-intent composition (AND/OR/back-to-back), CDA.

---

## 2. Experimental setup

- **Dataset:** `dataset/dataset.csv` — 500 labelled utterances with clean commands, multi-intent operators, named slots, STT noise injection, glued words, typos, and negative controls.
- **Harness:** `tinycfg_bench.cpp` — host-side C++ wrapping `TinyCFG::parse()` with high-resolution timing.
- **Recovery:** Fuzzy match ON, PER ON, error recovery ON (default TinyCFG configuration).
- **Metrics:** Label accuracy (pass/fail + expected action), latency (µs), confidence, recovery activations.

---

## 3. Figures

{fig_lines}

---

## 4. Tables

Full statistical tables: **[results/BENCHMARK_ANALYSIS.md](results/BENCHMARK_ANALYSIS.md)**

Includes:
- Table 1: Dataset composition
- Table 2: Accuracy by category
- Table 3: Latency by category (µs)
- Table 4: Robustness metrics (PER, DGS, noise skip)
- Table 5: Misclassified cases
- LaTeX table snippets (if generated with `--latex`)

---

## 5. Raw data

| File | Description |
|------|-------------|
| `dataset/dataset.csv` | Ground-truth labelled dataset |
| `results/results_raw.csv` | Per-case benchmark measurements |
| `results/figures/*.png` | Publication figures (300 DPI) |

---

## 6. Suggested paper text (Results section)

> We evaluated TinyCFG on a dataset of 500 voice commands spanning 11 categories
> including single-intent control, multi-intent composition (AND, OR, implicit
> back-to-back), named device slots, STT noise, glued-word segmentation, and
> phonetic/fuzzy recovery. On a host PC, the parser achieved **overall accuracy
> reported in Table 2** with mean latency **reported in Table 3** (µs per parse)
> and a fixed memory footprint of **{sizeof_parser} bytes**. Composition operators
> AND, OR, and juxtaposition were distinguished in the task tree (Figure 8).
> Recovery mechanisms (PER, DGS, gap-tolerant noise skip) activated primarily on
> noisy and glued inputs (Figure 6).

---

## 7. Reproduce

```bash
cd benchmark
python run_conference_analysis.py --iterations 200
```

Or on Windows after MinGW install:
```bat
build.bat
python run_conference_analysis.py
```

---

*TinyCFG benchmark package — ESpeech library*
"""


def main() -> int:
    ap = argparse.ArgumentParser(description="Full conference benchmark pipeline")
    ap.add_argument("--cases", type=int, default=500)
    ap.add_argument("--iterations", type=int, default=200)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--skip-build", action="store_true")
    args = ap.parse_args()

    DATASET_DIR.mkdir(parents=True, exist_ok=True)
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    FIGURES_DIR.mkdir(parents=True, exist_ok=True)

    dataset_path = DATASET_DIR / "dataset.csv"

    # 1. Generate dataset
    run([
        sys.executable, "generate_dataset.py",
        "-n", str(args.cases),
        "-o", str(dataset_path),
        "--seed", str(args.seed),
    ])

    # 2. Build benchmark if needed
    if not args.skip_build:
        bench_exe = ROOT / "tinycfg_bench.exe"
        if not bench_exe.exists() and not (ROOT / "tinycfg_bench").exists():
            print("\n>> Building tinycfg_bench...")
            try:
                run(["g++", "-std=c++17", "-O2", "-DTINYCFG_HOST_BENCH",
                     "-Ihost", "-I../src", "-o", "tinycfg_bench.exe",
                     "tinycfg_bench.cpp", "host/arduino_shim.cpp",
                     "../src/TinyCFG.cpp", "../src/TinyCFGDependency.cpp"])
            except subprocess.CalledProcessError:
                print("WARN: g++ build failed; expecting pre-built tinycfg_bench.exe")

    bench = find_bench_exe()
    results_csv = RESULTS_DIR / "results_raw.csv"

    # 3. Run benchmark
    run([str(bench), str(dataset_path), str(args.iterations), str(results_csv)])

    # 4. Analysis tables
    analysis_md = RESULTS_DIR / "BENCHMARK_ANALYSIS.md"
    run([
        sys.executable, "analyze_results.py",
        str(results_csv),
        "-o", str(analysis_md),
        "--latex",
    ])

    # 5. Figures
    run([
        sys.executable, "generate_graphs.py",
        str(results_csv),
        "-o", str(FIGURES_DIR),
    ])

    # 6. Conference master doc
    from analyze_results import read_results
    meta, _ = read_results(results_csv)
    figures = sorted(p.name for p in FIGURES_DIR.glob("fig*.png"))
    doc = build_conference_doc(meta, analysis_md, figures)
    conf_path = ROOT / "CONFERENCE_PAPER_RESULTS.md"
    conf_path.write_text(doc, encoding="utf-8")

    # Remove legacy root-level dataset if duplicated
    legacy = ROOT / "dataset.csv"
    if legacy.exists() and legacy.resolve() != dataset_path.resolve():
        legacy.unlink()

    print("\n" + "=" * 60)
    print("CONFERENCE PACKAGE COMPLETE")
    print("=" * 60)
    print(f"  Dataset:   {dataset_path}")
    print(f"  Raw CSV:   {results_csv}")
    print(f"  Tables:    {analysis_md}")
    print(f"  Figures:   {FIGURES_DIR}/ ({len(figures)} PNG)")
    print(f"  Paper doc: {conf_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
