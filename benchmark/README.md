# TinyCFG Benchmark — Full Reference

Host-side C++ evaluation suite for the **TinyCFG** pattern-augmented LL(1) voice-command parser.
Produces the dataset, latency measurements, accuracy tables, and publication-quality figures
for the MSc/conference paper.

---

## Folder layout

```
benchmark/
├── dataset/
│   └── dataset.csv              ← 500 labelled test cases (ground truth)
├── results/
│   ├── results_raw.csv          ← per-case measurements (generated)
│   ├── BENCHMARK_ANALYSIS.md    ← Tables 1–5 + LaTeX snippets (generated)
│   └── figures/                 ← 8 PNG graphs at 300 DPI (generated)
│       ├── fig01_dataset_composition.png
│       ├── fig02_accuracy_by_category.png
│       ├── fig03_latency_by_category.png
│       ├── fig04_latency_distribution.png
│       ├── fig05_confidence_by_category.png
│       ├── fig06_robustness_activations.png
│       ├── fig07_confusion_matrix.png
│       └── fig08_composition_operators.png
├── CONFERENCE_PAPER_RESULTS.md  ← Master paper results document
├── tinycfg_bench.cpp            ← C++ benchmark harness (compile & run)
├── generate_dataset.py          ← Dataset generator (500 realistic cases)
├── analyze_results.py           ← Produces statistical tables
├── generate_graphs.py           ← Produces 8 PNG figures
├── run_conference_analysis.py   ← Full pipeline in one command
├── run_conference_analysis.bat  ← Windows one-click runner
├── host/Arduino.h               ← Arduino API shim for PC compilation
├── build.bat                    ← Build with MinGW g++
├── build_msvc.bat               ← Build with Visual Studio cl.exe
└── requirements.txt             ← Python deps (matplotlib)
```

---

## Quick start

```bat
cd benchmark
run_conference_analysis.bat
```

Or manually:

```bash
pip install matplotlib
python run_conference_analysis.py --iterations 200
```

---

## Dataset category definitions

The 500 test cases are divided into **11 categories** that each test a distinct
capability of TinyCFG. Understanding what each class means is important for
interpreting the accuracy results.

---

### 1. `single_clean` — 55 cases — Accuracy: 85.5%

**What it is:**
A single, well-formed voice command with no noise, no multi-intent, and correct
standard vocabulary. This is the baseline category — the parser should handle
these with near-perfect accuracy.

**What TinyCFG must do:**
Tokenize the text, look up terminals by exact synonym match, run pattern matching,
and dispatch one action.

**Examples:**
```
turn on room light
dock
arm alarm
play music
check garage camera
turn on rakibs back fan
```

**Why accuracy < 100%:**
Some single_clean cases include names or locations (like `"go to alex office"`)
that happen to conflict with unusual slot resolution, causing a few misses.

---

### 2. `multi_and` — 45 cases — Accuracy: 93.3%

**What it is:**
Two voice commands joined by the word **"and"** — meaning execute **both** commands.
This tests the parser's N-intent composition with explicit conjunction.

**Grammar operator:** `TCFG_SEQ_AND`
**Semantics:** Both actions are dispatched. The task tree contains `ACTION → OP(and) → ACTION`.

**Examples:**
```
turn on room light and come here
patrol hallway and go home
play music and volume up
arm alarm and check garage camera
```

**Why this matters:**
In real IoT/robotics control users naturally chain commands: `"turn on the fan and go to the kitchen"`.
The parser must correctly segment both commands and attach the right operator.

---

### 3. `multi_or` — 25 cases — Accuracy: 100%

**What it is:**
Two voice commands joined by **"or"** — meaning pick one (exclusive choice).
The parser records both alternatives in the tree but dispatches only the first.

**Grammar operator:** `TCFG_SEQ_OR`
**Semantics:** Alternative commands; the first is executed. Useful for conditional device
control: `"turn on the fan or open the window"`.

**Examples:**
```
heat up or cool down
come here or go home
arm alarm or disarm alarm
play music or pause music
```

**Why 100%:**
The `or` keyword maps to a unique terminal (`TCFG_TOK_OR`) and both sub-commands
are clean, so pattern matching succeeds on all 25 cases.

---

### 4. `multi_juxta` — 35 cases — Accuracy: 100%

**What it is:**
Two commands placed **back-to-back with no connector word** — the parser must detect
a second command starting immediately after the first without any `and`, `or`, `then` etc.

**Grammar operator:** `TCFG_SEQ_JUXTA` (implicit juxtaposition)
**Detection mechanism:** `canStartCommandAt()` checks if a compiled pattern matches at
the current cursor position after the first command ends.

**Examples:**
```
play music volume up
dock stop
arm alarm check garage camera
turn on office light turn off kitchen fan
```

**Why this matters:**
STT output sometimes omits connector words. This is the hardest structural test —
no explicit operator token — yet TinyCFG achieves 100%.

---

### 5. `named_slot` — 55 cases — Accuracy: 83.6%

**What it is:**
Commands that include a **person's name** or a **custom device label** that is not
a known grammar terminal. The parser captures this as a `{name}` slot and
builds the action argument as `{name}_{location}_device`.

**Slot type:** `TCFG_SLOT_NM` — any unknown non-digit token is a valid name.

**Examples:**
```
turn on rakibs room fan       → FAN_ON(rakibs_room_fan)
unlock david door             → UNLOCK(david_door)
go to sarahs office           → NAV_GO(sarah_office)
play kate playlist            → MEDIA_PLAY(kate_playlist)
```

**Why accuracy < 100%:**
Some generated names (e.g. `"omar"`, `"marias"`) appear as unknown tokens that the
gap-tolerant matcher sometimes skips as noise instead of capturing as slot values,
causing occasional mismatches.

---

### 6. `stt_noise` — 80 cases — Accuracy: 73.8%

**What it is:**
Real-world **Speech-to-Text (STT) output corruption** — the largest and hardest
category. Includes:

| Noise type | Example |
|------------|---------|
| Random garbage tokens | `adjfija`, `xxx`, `$$$` |
| Pure digit injection | `120`, `09309582903`, `42`, `999` |
| Filler words | `um`, `uh`, `hmm`, `like`, `well`, `okay` |
| Uppercase (ASR output) | `TURN OFF BEDROOM LIGHT NOW` |
| Mixed case / title case | `Okay Xxx Turn Off Living Fan` |
| Noise before/after command | `uh go to front right like this now okay` |

**Recovery mechanism used:** Gap-tolerant pattern matching (`tryMatchPatternAt`) skips
up to `TCFG_MAX_NOISE_SKIP = 12` unknown/digit/filler tokens between expected
pattern literals. Filler words are blocked by `isFillerWord()`.

**Examples:**
```
OKAY UM RECORD LIVING CAMERA RIGHT NOW ER ADJFIJA PLEASE
xxx adjfija could you hmm record bathroom camera now
THE LIKE OKAY UM LIKE GO HOME PLEASE NOW
i want to 999 scene party 09309582903 hmm please thanks
```

**Mean confidence: 0.263** — This category has the lowest confidence score because
many tokens are skipped, reducing the confidence formula.

**Why accuracy = 73.8%:** Extreme multi-noise cases overwhelm the skip budget (12 tokens).
This is the realistic performance floor for STT-corrupted voice commands.

---

### 7. `glued_dgs` — 45 cases — Accuracy: 88.9%

**What it is:**
Commands where words are **glued together** without spaces — a common STT failure mode.
Includes camelCase variants (when ASR incorrectly capitalises mid-word) and
fully lowercase runs.

**Recovery mechanism used:** DGS — De-Glued Segmentation
- `expandCamelCase()` — splits `TurnOnRoomLight` → `turn on room light`
- `segmentGluedWord()` — greedy dictionary match: `turnonfan` → `turn + on + fan`
- `expandInlineGluedTokens()` — applies per-token after spaced split

**Examples:**
```
turnOffHallwayFan       → turn off hallway fan   (camelCase)
coolDown                → cool down               (camelCase)
goHome                  → go home                 (camelCase)
checkgaragecamera       → check garage camera     (greedy dict match)
turnonfan               → turn on fan             (greedy dict match)
playMariasPlaylist      → play marias playlist    (camelCase + name slot)
```

**DGS segments activated: 18** — 18 sub-words were produced by degluing.

---

### 8. `fuzzy_per` — 45 cases — Accuracy: 77.8%

**What it is:**
Commands with **spelling mistakes and phonetic errors** as produced by STT engines
or non-native speakers. Tests two recovery layers:

| Layer | Mechanism | Example |
|-------|-----------|---------|
| **PER** | Phonetic Error Recovery (Soundex-like) | `"lite"` → `light`, `"com"` → `come` |
| **Fuzzy** | Edit-distance (max 2 substitutions/deletions) | `"swich"` → `switch`, `"bedrom"` → `bedroom` |

**PER hits: 28** — 28 tokens were recovered via phonetic lookup.
**Mean confidence: 0.633** — Lower than clean categories due to recovery penalties.

**Examples:**
```
swich on bedrom light     → switch on bedroom light   (2 typos, fuzzy)
turn on lite and com here → LIGHT_ON + COME_HERE      (fuzzy + PER)
coem heer                 → come here                  (PER both words)
fllow me                  → follow me                  (fuzzy)
lck garden dor            → lock garden door           (fuzzy×2)
```

**Why accuracy = 77.8%:** Extreme multi-typo cases (e.g. `"tun on office fans"`)
exceed the edit-distance tolerance or produce wrong matches.

---

### 9. `temporal_then` — 35 cases — Accuracy: 82.9%

**What it is:**
Two commands joined by **"then"** — meaning execute the first, then the second
in sequence. Temporal ordering is recorded in the task tree for CDA analysis.

**Grammar operator:** `TCFG_SEQ_THEN`
**CDA:** Creates a sequential dependency `task0 → then → task1` in `TcfgDependencyReport`.

**Examples:**
```
turn off priya bedroom fan then turn on kitchen light
pause music then next song
disarm alarm then check cameras
set timer for five minutes then turn off room fan
go to living room then follow me
```

**Mean P95 latency: 86.2 µs** — Slightly higher than `multi_and` because `then`
triggers CDA dependency analysis on two tasks.

---

### 10. `cross_domain` — 35 cases — Accuracy: 91.4%

**What it is:**
Multi-intent commands that span **two different grammar domains** — for example
smarthome + robot, media + smarthome, or security + navigation. Tests that
the parser correctly handles patterns from multiple domains in a single utterance.

**Domains available:** smarthome, robot, media, security, climate, timer.

**Examples:**
```
turn on kitchen light and come here          (smarthome + robot)
scene movie and turn on living light         (smarthome×2, different resources)
go to kitchen and follow me                  (robot nav + robot follow)
set timer for five minutes then turn off fan (timer + smarthome)
disarm alarm then check cameras             (security×2)
```

**Mean confidence: 0.994** — Very high because both sub-commands are
clean and well-formed; no noise or typos.

---

### 11. `negative` — 45 cases — Accuracy: 51.1%

**What it is:**
Utterances that are **not valid commands** and should cause `parse()` to return
`false` (0 action nodes). Tests that the parser **rejects** out-of-grammar input
and does not hallucinate commands.

**Expected result:** `expect_pass = 0` — these should FAIL.

**Examples:**
```
random gibberish words       → no pattern match → FAIL ✓
hello world foo bar          → no pattern match → FAIL ✓
turn on the weather          → unknown device → FAIL ✓
make me coffee               → out of grammar → FAIL ✓
what is the time             → question form → FAIL ✓
send email to boss           → office task → FAIL ✓
tell me a joke               → conversational → FAIL ✓
```

**Why only 51.1% accuracy:**
Many negative cases contain words like `"go"`, `"turn"`, `"lock"` that are real
grammar terminals. The gap-tolerant matcher sometimes assembles a partial match
from fragments — a **false positive**. For example:
- `"what is the time please now"` — `"please"` and `"now"` are fillers, and
  the remaining words may accidentally match a pattern.

This is a known limitation of gap-tolerant parsing: it favours recall over precision.
**False positives (FP = 22) are the primary failure mode for this category.**

---

## Benchmark result summary

| # | Category | Cases | Accuracy | Mean µs | Confidence | PER hits | DGS segs | Noise skip |
|---|----------|-------|----------|---------|------------|----------|----------|------------|
| 1 | single_clean | 55 | **85.5%** | 22.7 | 0.934 | 3 | 0 | 3 |
| 2 | multi_and | 45 | **93.3%** | 50.4 | 1.000 | 0 | 0 | 0 |
| 3 | multi_or | 25 | **100.0%** | 42.8 | 1.000 | 0 | 0 | 0 |
| 4 | multi_juxta | 35 | **100.0%** | 39.5 | 1.000 | 0 | 0 | 0 |
| 5 | named_slot | 55 | **83.6%** | 32.8 | 0.714 | 14 | 0 | 27 |
| 6 | stt_noise | 80 | **73.8%** | 63.4 | 0.263 | 70 | 0 | 263 |
| 7 | glued_dgs | 45 | **88.9%** | 26.9 | 0.964 | 1 | 18 | 1 |
| 8 | fuzzy_per | 45 | **77.8%** | 33.9 | 0.633 | 28 | 0 | 39 |
| 9 | temporal_then | 35 | **82.9%** | 54.3 | 0.981 | 1 | 0 | 3 |
| 10 | cross_domain | 35 | **91.4%** | 57.0 | 0.994 | 0 | 0 | 1 |
| 11 | negative | 45 | **51.1%** | 37.8 | 0.575 | 33 | 8 | 25 |
| — | **OVERALL** | **500** | **82.6%** | **42.3** | **0.786** | **150** | **26** | **362** |

---

## Column definitions (results_raw.csv)

| Column | Meaning |
|--------|---------|
| `id` | Row number in dataset |
| `category` | Test class (see above) |
| `input` | Raw text sent to `parse()` |
| `expect_pass` | Ground truth: 1 = should parse, 0 = should fail |
| `actual_pass` | What TinyCFG actually returned |
| `action_count` | Number of ACTION nodes in task tree |
| `first_action` | First matched action name (e.g. `LIGHT_ON`) |
| `mean_us` | Mean parse latency over all iterations (µs) |
| `min_us` / `max_us` | Best / worst single parse (µs) |
| `std_us` | Standard deviation of latency |
| `p95_us` | 95th percentile latency (µs) |
| `tokens` | Number of tokens after tokenization |
| `confidence` | Parser confidence score 0.0–1.0 |
| `ram_bytes` | Estimated RAM use for this parse |
| `phonetic_hits` | Tokens recovered via PER (Soundex) |
| `glued_segments` | Sub-words produced by DGS deglue |
| `noise_skipped` | Noise tokens skipped during pattern matching |
| `description` | Human-readable case description |

---

## Reproduce

```bat
cd benchmark
run_conference_analysis.bat
```

All outputs go into `dataset/`, `results/`, and `CONFERENCE_PAPER_RESULTS.md`.
