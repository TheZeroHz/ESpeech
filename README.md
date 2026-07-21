# ESP32 Speech-To-Text (No API Key Required)

Industrial-grade speech-to-text pipeline for ESP32. This repository provides:

* An ESP32 client that captures audio over I2S and posts WAV to a server.
* A lightweight Flask/Gunicorn server that returns JSON transcriptions via `speech_recognition`.

Designed for deterministic embedded behavior, clean I2S lifecycle, and zero vendor lock-in.

---

## Overview

* **Client**: ESP32 (Arduino) captures 16-bit mono audio and uploads to a server.
* **Server**: Flask endpoint processes audio and returns transcription (`/uploadAudio`).
* **Wake Word (optional)**: Integrate the MARVIN wake word for hands-free activation.

### Server Repository

* **ESpeechServer**: [https://github.com/TheZeroHz/ESpeechServer](https://github.com/TheZeroHz/ESpeechServer)
  Deploy locally or to cloud (recommended), e.g. **Render**.

### Wake Word Library (Optional)

* **Marvin\_WakeWord\_inferencing**: [https://github.com/TheZeroHz/Marvin\_WakeWord\_inferencing](https://github.com/TheZeroHz/Marvin_WakeWord_inferencing)

---

## Compatibility

* **ESP32 Arduino Core**: **3.3.1** (recommended/supported)
* **Arduino IDE**: 2.3.x
* **Boards**:

  * ESP32-S3 — **validated**
  * ESP32 DOIT DevKit V1 — under test
* **I2S Microphone**: INMP441 (or compatible)

> If you previously targeted ESP32 core **2.0.14**, upgrade to **3.3.1** for best results.

---

## Features

* Low-latency I2S capture with deterministic init/deinit (prevents double driver install).
* No accounts, credit cards, or external API keys required.
* Simple HTTP interface (`POST /uploadAudio`) returning JSON.
* Easily deployable backend with **Gunicorn**.
* Optional wake word handoff (pause WW → record STT → resume WW).

---

## Demo & Tutorial

* **Video (all ESP32 boards)**:
  [https://www.canva.com/design/DAGkKUr6V58/pw6ovNUVmsN3kMa85Zlr7w/watch?utm\_content=DAGkKUr6V58\&utm\_campaign=designshare\&utm\_medium=link2\&utm\_source=uniquelinks\&utlId=h054e4457dc](https://www.canva.com/design/DAGkKUr6V58/pw6ovNUVmsN3kMa85Zlr7w/watch?utm_content=DAGkKUr6V58&utm_campaign=designshare&utm_medium=link2&utm_source=uniquelinks&utlId=h054e4457dc)

* **Screenshots**
  ![Demo1](SpeechToText/img/D1.PNG)
  ![Demo2](SpeechToText/img/D2.PNG)

---

## Requirements

### Server

* **Python**: 3.10 (recommended; set `PYTHON_VERSION=3.10` on Render)
* Packages: `Flask`, `SpeechRecognition`, `pydub`, `gunicorn`

### ESP32

* **ESP32 Arduino Core**: **3.3.1**
* **Arduino IDE**: 2.3.x
* **I2S MIC**: INMP441 (or equivalent)
* Stable Wi-Fi

---

## Server Setup

> Use the **ESpeechServer** repository.

1. Clone:

   ```bash
   git clone https://github.com/TheZeroHz/ESpeechServer.git
   cd ESpeechServer
   ```
2. Install:

   ```bash
   pip install -r requirements.txt
   ```
3. Run (production style):

   ```bash
   gunicorn app:app --bind 0.0.0.0:8888
   ```
4. Endpoint:

   * `POST /uploadAudio` (content: WAV) → `{"transcription": "..."}`

### Deploy on Render (Recommended)

* **Build Command**: `pip install -r requirements.txt`
* **Start Command**: `gunicorn app:app`
* **Environment Variable**: `PYTHON_VERSION=3.10`
* Server listens on `PORT` provided by Render automatically.

---

## ESP32 Client Setup

1. Open **Arduino IDE** (2.3.x).
2. Install **ESP32 Arduino Core 3.3.1** via Boards Manager.
3. Open the `SpeechToText_ESP32` example in this repository.
4. Configure:

   * **Wi-Fi** SSID/PASS
   * **Server URL** (local or Render), e.g.:

     ```cpp
     STT.serverURL("https://<your-espeechserver>/uploadAudio");
     ```
   * **I2S pins** to match your hardware (SCK/BCK, WS, SD).
5. Build & flash.

---

## Usage Flow

1. (Optional) Run wake word detection loop.
2. On trigger:

   * Stop WW loop, **deinit I2S** cleanly.
   * Call `STT.recordAudio()` to capture STT audio.
   * Call `STT.getTranscription()` to receive server JSON → string.
   * Re-init WW loop if required.

> Avoid simultaneous ownership of the I2S port to prevent `i2s_driver_install` errors.

---

## API (Server)

* **POST** `/uploadAudio`
  **Body**: WAV (binary or multipart).
  **Response**:

  ```json
  { "transcription": "Hello, how are you?" }
  ```

**curl example**:

```bash
curl -X POST http://localhost:8888/uploadAudio --data-binary "@yourfile.wav"
```

---

## Hardware Reference

* Example wiring shown for **INMP441** + **ESP32-S3** (see: `SpeechToText/img/HardWareSetUP.png`).
  Ensure your I2S pin mapping in the sketch matches your board.

![Hardware Setup](SpeechToText/img/HardWareSetUP.png)

---

## Troubleshooting

* **`i2s port is in use` / `i2s_driver_install(...): configuration is invalid`**

  * Ensure the wake word task is stopped and `i2s_driver_uninstall()` completed **before** ESpeech initializes I2S.
  * Do not install the I2S driver twice.

* **No transcription returned**

  * Verify server reachability and URL.
  * Confirm WAV parameters (16-bit, mono, 8/16 kHz).
  * Check server logs for decoding/engine errors.

* **Board/core mismatch**

  * Use ESP32 Arduino Core **3.3.1**.

---

## Change Log (Summary)

* Align with **ESP32 Arduino Core 3.3.1**
* Deterministic I2S init/deinit to prevent double-install
* Example updates for wake word ↔ STT handoff
* Cleaned includes and configuration to avoid FS conflicts

---

---

## TinyCFG — Class & Concept Definitions

TinyCFG is the lightweight LL(1) voice-command parser bundled with this project.
Below is a plain-English guide to every class, enum, and key concept used in the library.

---

### Core classes

#### `TinyCFG`
The main parser class. You create one instance, call `parse(text)`, and read the results.

| Method | What it does |
|--------|-------------|
| `parse(text)` | Tokenize → DGS expand → PER/fuzzy recover → pattern match → build task tree. Returns `true` if at least one action was found. |
| `printPipeline(Serial)` | Print full data-flow trace: raw → normalized → tokens → pattern → actions |
| `printDataFlow(Serial)` | Print step-by-step data transformation (raw text → final action names) |
| `printActionsSummary(Serial)` | Print matched actions with confidence and operator hints |
| `executeActions(callback)` | Walk the task tree and call your callback for each ACTION node |
| `dispatchActions(Serial)` | Operator-aware print: AND executes all, OR executes first, THEN in order |

**Internal pipeline stages:**

```
Raw text
  └─ normalize (lowercase, strip punctuation)
       └─ DGS expand (split glued/camelCase words)
            └─ tokenize
                 └─ PER / fuzzy recovery (fix phonetic / typo errors)
                      └─ pattern match (LL(1) grammar)
                           └─ slot capture (names, locations)
                                └─ task tree build
                                     └─ CDA (dependency analysis)
```

---

#### `TcfgTaskNode`
A node in the hierarchical **task tree** produced after a successful parse.

| Field | Type | Meaning |
|-------|------|---------|
| `type` | `TcfgNodeType` | What kind of node this is (ACTION, SLOT, SEQ, ARGS, ROOT) |
| `label` | `char[]` | Human-readable label: action name, slot value, or operator name |
| `children[]` | `TcfgTaskNode*` | Child nodes (e.g. arguments of an action, sub-commands of a sequence) |
| `confidence` | `float` | 0.0–1.0, how certain the parser is about this node |
| `seqOp` | `TcfgSeqOp` | For SEQ nodes: which composition operator links the children |

---

#### `TcfgParseStats`
Lightweight metrics collected during a `parse()` call — available after parsing.

| Field | Meaning |
|-------|---------|
| `tokenCount` | Number of tokens after expansion |
| `noiseSkipped` | Tokens skipped by gap-tolerant matcher |
| `phoneticHits` | Tokens recovered by PER (Soundex) |
| `gluedSegments` | Sub-words produced by DGS degluing |
| `ramUsed` | Estimated peak RAM for this parse (bytes) |
| `parseTimeUs` | Wall-clock parse time in microseconds |
| `confidence` | Overall confidence (product of per-node scores) |

---

#### `TcfgDependencyReport`  *(from `TinyCFGDependency.h`)*
Produced by `TinyCFGDependency::analyze(taskTree)`.  Identifies which commands
in a multi-intent utterance share devices or conflict with each other.

| Field | Meaning |
|-------|---------|
| `edges[]` | Directed edges between task nodes (`node_a` → `node_b`) |
| `conflicts[]` | Pairs of commands that are mutually exclusive (e.g. LIGHT_ON and LIGHT_OFF for same device) |
| `conflictType` | `TcfgConflictType` enum value for each conflict |
| `sequenceOp` | Operator that links the two tasks (`TCFG_SEQ_AND`, `TCFG_SEQ_OR`, `TCFG_SEQ_THEN`, …) |

---

### Enums

#### `TcfgNodeType` — what kind of task-tree node

| Value | Meaning |
|-------|---------|
| `TCFG_NODE_ROOT` | Root of the task tree (always present) |
| `TCFG_NODE_ACTION` | A matched voice command action (e.g. `LIGHT_ON`, `FAN_OFF`) |
| `TCFG_NODE_SEQ` | A sequence connector linking two sub-trees |
| `TCFG_NODE_ARGS` | Argument group (device, location, slot values) |
| `TCFG_NODE_SLOT` | A named slot value (person name, custom label) |

---

#### `TcfgSeqOp` — how two commands are composed

| Value | Keyword | Meaning |
|-------|---------|---------|
| `TCFG_SEQ_NONE` | *(none)* | Single command, no composition |
| `TCFG_SEQ_AND` | `and` | Execute both commands |
| `TCFG_SEQ_OR` | `or` | Choose one alternative; first is dispatched |
| `TCFG_SEQ_THEN` | `then` | Execute first, then second in order |
| `TCFG_SEQ_AFTER` | `after` | Second triggers when first completes |
| `TCFG_SEQ_BEFORE` | `before` | Explicit temporal ordering |
| `TCFG_SEQ_IF` | `if` | Conditional: second only if first succeeds |
| `TCFG_SEQ_UNTIL` | `until` | Loop first until second condition met |
| `TCFG_SEQ_WHILE` | `while` | Execute first only while condition holds |
| `TCFG_SEQ_JUXTA` | *(none — implicit)* | Two commands detected back-to-back with no operator keyword |

---

#### `TcfgConflictType` — type of semantic conflict

| Value | Meaning |
|-------|---------|
| `TCFG_CONFLICT_NONE` | No conflict |
| `TCFG_CONFLICT_TOGGLE` | ON and OFF for the same device in the same utterance |
| `TCFG_CONFLICT_RESOURCE` | Two actions compete for the same physical resource |
| `TCFG_CONFLICT_TEMPORAL` | Ordering conflict (A before B but B preconditions A) |

---

### Benchmark dataset classes

See `benchmark/README.md` for the full definition of each test category.
Quick summary:

| Category | What is tested | expect_pass |
|----------|---------------|-------------|
| `single_clean` | One clean command, correct vocab | 1 |
| `multi_and` | Two commands joined by `and` | 1 |
| `multi_or` | Two commands joined by `or` | 1 |
| `multi_juxta` | Two commands with no connector (implicit) | 1 |
| `named_slot` | Commands containing a person's name as a slot | 1 |
| `stt_noise` | Garbled STT output: fillers, garbage tokens, digits | 1 |
| `glued_dgs` | CamelCase or run-together words (DGS recovery) | 1 |
| `fuzzy_per` | Spelling mistakes and phonetic substitutions (PER recovery) | 1 |
| `temporal_then` | Two commands joined by `then` (ordered execution) | 1 |
| `cross_domain` | Multi-intent commands spanning two grammar domains | 1 |
| `negative` | Out-of-grammar utterances — parser should reject them | 0 |

---

## License

This repository: MIT (see `LICENSE`).
3rd-party libraries retain their respective licenses.
