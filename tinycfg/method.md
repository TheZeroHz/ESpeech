# TinyCFG Methodology — Complete Technical Reference

This document explains **what TinyCFG is**, **how each pipeline step works**, **extreme test cases with walkthroughs**, and **every related function** in the ESpeech library.

---

## 1. What is TinyCFG?

**TinyCFG** is a lightweight, **deterministic** voice-command parser for ESP32-class devices. It converts natural-language text (from Serial, STT, or any source) into a **hierarchical task tree** and **executable semantic actions** — without cloud services or machine learning at parse time.

### Design goals

| Goal | How TinyCFG achieves it |
|------|-------------------------|
| Lightweight | Fixed memory pools, no heap; ~4 KB RAM typical |
| Deterministic | LL(1) grammar + compiled pattern tables |
| Multi-intent (N-intent) | Operators: `and`, `or`, `then`, `after`, `before`, `if`, `until`, `while`, plus implicit back-to-back |
| Real-world device names | Slot patterns: `{name}`, `{location}`, `{device}` |
| STT robustness | PER, fuzzy match, noise skip, glued-word segmentation |
| Explainable | Human-readable task tree + metrics + data-flow trace |
| Embedded-ready | Arduino C++, offline grammar compilation |

---

## 2. System architecture (two phases)

```
┌─────────────────────────────────────────────────────────────────┐
│  PHASE A — OFFLINE (PC / build machine)                         │
│                                                                 │
│  robot.cfg / smart_world.cfg                                    │
│         │                                                       │
│         ▼                                                       │
│  tools/tinycfg_compiler.py                                      │
│    • Parse terminals, rules, patterns, slots                    │
│    • Compute FIRST sets, detect LL(1) conflicts                  │
│    • Emit compact C header: smart_world_grammar.h               │
│         │                                                       │
│         ▼                                                       │
│  src/grammars/smart_world_grammar.h  (flash on ESP32)           │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  PHASE B — ONLINE (ESP32 runtime)                               │
│                                                                 │
│  Input text                                                     │
│    → Tokenizer (+ DGS deglue)                                   │
│    → Lexical lookup (synonyms + PER + fuzzy)                    │
│    → LL(1) pattern matcher (gap-tolerant)                       │
│    → Hierarchical task tree (and / or / back-to-back)         │
│    → CDA (Command Dependency Analyzer)                          │
│    → Semantic action dispatch (your callback)                   │
│    → Data-flow trace (optional Serial output)                   │
└─────────────────────────────────────────────────────────────────┘
```

### Full pipeline with ESpeech (voice robot)

```
Microphone → ESpeech STT → text string → TinyCFG.parse() → Task tree → Robot/IOT
```

TinyCFG does **not** require STT. Use `examples/TinyCFG_CLI_Test` to test grammar-only over Serial. Every parse can show a **data-flow diagram** (stages 0–9) plus an **8-step pipeline trace**.

---

## 3. Data flow diagram (input → output)

**Purpose:** Show how the input string is **transformed at each stage** before semantic dispatch. Useful for debugging STT noise, DGS deglue, and multi-intent parsing.

**API:** `printDataFlow(Stream&)`, `getDataFlowStage(0..9)`, integrated into `printPipeline()`.

| Stage | Name | Transformation |
|-------|------|----------------|
| **0** | Raw input | Original string as received |
| **1** | Normalized | Lowercase; punctuation/apostrophes stripped |
| **2** | Spaced words (pre-DGS) | `[turnonfan]` `[and]` … before deglue |
| **3** | DGS transforms | `turnonfan -> turn+on+fan` |
| **4** | Token stream | Final tokens after expansion |
| **5** | Lexical mapping | `"lite" --[FUZZY]--> light(id=36)` |
| **6** | Semantic matches | `LIGHT_ON(room_light) and FAN_OFF(room_fan)` |
| **7** | Task tree | Compact tree line |
| **8** | CDA result | `tasks=2 deps=1 conflicts=0 safe=YES` |
| **9** | Output / dispatch | `EXEC LIGHT_ON(...), EXEC FAN_OFF(...)` |

**Example (Serial output shape):**

```
[0] RAW INPUT
    "120 turnonfan and turnoffroomlight"
         |
         v  lowercase, strip punctuation/apostrophes
[1] NORMALIZED TEXT
    "120 turnonfan and turnoffroomlight"
         |
         v  split on spaces / punctuation
[2] SPACED WORDS (pre-DGS)
    [120] [turnonfan] [and] [turnoffroomlight]
         |
         v  DGS: camelCase split + dictionary deglue
[3] DGS TRANSFORMS
    turnonfan -> turn+on+fan; turnoffroomlight -> turn+off+room+light
...
[9] OUTPUT / DISPATCH
    EXEC FAN_ON(default_fan), EXEC LIGHT_OFF(room_light)
```

**Related functions:**

| Function | Role |
|----------|------|
| `buildDataFlow()` | Build snapshots after `parse()` |
| `capturePreGlueTokens()` | Save pre-DGS token state during tokenization |
| `printDataFlow()` | Print ASCII stage diagram |
| `getDataFlowStage(n)` | Programmatic access to stage `n` string |
| `printPipeline()` | Data flow + full 8-step trace |

**CLI:** `flow <sentence>` prints only the data-flow diagram; `parse <sentence>` prints data flow + pipeline.

---

## 4. Pipeline steps (detailed)

### Step 1 — Voice / text input

**Input:** Any UTF-8-ish ASCII string.

**Examples:**
- Clean: `"turn on room light"`
- Multi-intent: `"turn on kitchen fan and turn off room light"`
- Composition variants: `"turn on room light and turn off room fan"` / `... or ...` / `... turn off ...` (no connector)
- Noisy STT: `"TURN $$$ ADJFIJA ON 09309582903 ROOM LIGHT"`
- Glued: `"TurnOnRoomLight"` or `"turnonfan"`
- Extreme: `"120 turnonfan and after this turnoffroomlight"`

**Related function:** `TinyCFG::parse(const char* text)` — entry point.

---

### Step 2 — Tokenization

**Purpose:** Split raw text into a sequence of lexical tokens.

**Sub-stages:**

#### 2a. Spaced tokenization (`tokenizeSpaced`)

- Lowercases input
- Splits on non-alphanumeric boundaries (spaces, punctuation)
- Strips apostrophes (`rakib's` → `rakibs`)

#### 2b. Glued-word segmentation — DGS (`expandInlineGluedTokens`)

When individual tokens are glued together:

| Input token | DGS output |
|-------------|------------|
| `turnonfan` | `turn` `on` `fan` |
| `turnoffroomlight` | `turn` `off` `room` `light` |
| `TurnOnRoomLight` | camelCase expand → `turn` `on` `room` `light` |
| `tTurnOnRoomLight` | `t` `turn` `on` `room` `light` (leading `t` skipped as noise) |

**Algorithm:** Greedy longest-match against the compiled synonym dictionary.

#### 2c. Full-string deglue (`tokenizeGlued`)

If the **entire input** has no spaces and length ≥ 5 (e.g. `turnonroomlight`), dictionary segmentation runs on the whole string.

**Related functions:**

| Function | Role |
|----------|------|
| `tokenize()` | Routes to spaced / glued / camelCase path |
| `tokenizeSpaced()` | Word-boundary split + inline deglue |
| `tokenizeGlued()` | Full-string dictionary segmentation |
| `expandCamelCase()` | `TurnOnFan` → `turn on fan` |
| `expandInlineGluedTokens()` | Per-token deglue after spaced split |
| `segmentGluedWord()` | Greedy dict match for one glued word |
| `wordNeedsGluedSegmentation()` | Returns true if word is unknown and length ≥ 6 |
| `pushToken()` | Appends one token to internal buffers |

**Internal buffers:**
- `tokenBuf[]` — terminal ID per token
- `tokenText[][]` — original word text (for slots & debug)
- `tokenFlags[]` — `EXACT`, `FUZZY`, or `PHON`

---

### Step 3 — Lexical lookup & normalization

**Purpose:** Map each word to a grammar terminal ID.

**Order of resolution (`lookupToken`):**

1. **Exact synonym** — e.g. `switch` → `turn`, `lamp` → `light`
2. **Reject noise** — pure digits (`120`), filler words (`this`, `the`, `please`)
3. **PER (Phonetic Error Recovery)** — Soundex-like key match (`phoneticLookup`)
4. **Fuzzy edit-distance** — tolerates 1–2 char typos (`editDistanceLookup`)

**Related functions:**

| Function | Role |
|----------|------|
| `lookupToken()` | Main lexical resolver |
| `phoneticLookup()` | Soundex-style phonetic match |
| `editDistanceLookup()` | Character-distance fuzzy match |
| `isPureDigits()` | Detect numeric noise |
| `isFillerWord()` | Detect STT filler (`this`, `um`, etc.) |

**Example:**

```
Input word: "lite"
  → fuzzy match → terminal LIGHT

Input word: "120"
  → isPureDigits → TCFG_TOK_UNKNOWN (skipped later)

Input word: "adjfija"
  → no match → UNKNOWN (skipped during pattern matching)
```

**Toggle recovery:**

```cpp
parser.setFuzzyMatch(true);       // edit-distance
parser.setPhoneticRecovery(true); // PER
parser.setErrorRecovery(true);    // gap/noise skip in parser
```

---

### Step 4 — LL(1) pattern-augmented parsing

**Purpose:** Match token stream against **74 compiled command patterns** and build a task list.

**Grammar structure (from `smart_world.cfg`):**

```ini
TASK_LIST  -> TASK OP_REST
OP_REST    -> OP TASK OP_REST
OP_REST    ->                    # epsilon (end)
```

**Composition operators — three distinct multi-intent forms:**

These are **not** interchangeable. The parser records a different `TcfgSeqOp` and applies different dispatch semantics.

| User says | Operator in tree | `TcfgSeqOp` | Semantics |
|-----------|------------------|-------------|-----------|
| `... and ...` | `OP(and)` | `TCFG_SEQ_AND` | Execute **both** commands (parallel conjunction) |
| `... or ...` | `OP(or)` | `TCFG_SEQ_OR` | **Exclusive choice** — pick one alternative |
| `... ...` (no word) | `OP(back-to-back)` | `TCFG_SEQ_JUXTA` | **Implicit sequence** — two commands spoken back-to-back |

**Examples (same two actions, different meaning):**

```
turn on room light and turn off room fan   → LIGHT_ON + OP(and) + FAN_OFF
turn on room light or turn off room fan    → LIGHT_ON + OP(or) + FAN_OFF
turn on room light turn off room fan       → LIGHT_ON + OP(back-to-back) + FAN_OFF
```

**Implicit back-to-back detection:** When `parseOpRest()` finds no explicit operator token but `canStartCommandAt(cursor)` is true (another command pattern matches at the current position), the parser inserts `TCFG_SEQ_JUXTA` without consuming a token.

**Explicit `or` terminal:** Added to `smart_world.cfg` with synonym `alternatively`. Compiled to `TCFG_TOK_OR` in the grammar header.

**Pattern example:**

```ini
turn on {location} light => LIGHT_ON({location}_light) @domain smarthome @resource light
turn on {name} {location} fan => FAN_ON({name}_{location}_fan) @domain smarthome @resource fan
```

**Slot types:**

| Slot | Matches | Example capture |
|------|---------|-----------------|
| `{location}` | Known zones: kitchen, bedroom, room... | `kitchen` |
| `{name}` | Any unknown non-digit word | `rakibs`, `john` |
| `{device}` | fan, light, door, curtain... | `fan` |

**Pattern matching (`matchPattern` / `tryMatchPatternAt`):**

- Tries all patterns starting at current cursor
- Picks the **longest successful match**
- **Gap-tolerant:** skips noise tokens between literals (digits, unknown garbage, fuzzy false-positives)
- Substitutes `{location}`, `{name}` into arg template → e.g. `rakibs_room_fan`

**Related functions:**

| Function | Role |
|----------|------|
| `parseTaskList()` | Parse first command + operator chain |
| `parseSingleTask()` | Find & match one command pattern |
| `parseOpRest()` | Handle `and` / `or` / `then` / … or implicit back-to-back |
| `canStartCommandAt()` | Detect if another command pattern starts at position |
| `matchPattern()` | Best pattern selection at cursor |
| `tryMatchPatternAt()` | Single pattern with gap skipping |
| `tokenMatchesPattern()` | Literal or slot match test |
| `isLocationToken()` | Zone terminal check |
| `isValidNameToken()` | User-name slot check |
| `isDeviceToken()` | Device terminal check |
| `buildArgFromTemplate()` | `FAN_ON({name}_{location}_fan)` → `FAN_ON(rakibs_room_fan)` |
| `peek()` / `consumeOp()` | Token stream navigation |
| `peekOp()` / `isOpToken()` | Detect composition operators |
| `skipNoise()` | Advance cursor past skippable tokens |
| `isSkippableNoise()` | Digit / filler / unknown check |
| `shouldSkipForPattern()` | Context-aware skip during pattern match |
| `countActionNodes()` | Count real actions in tree |

---

### Step 5 — Hierarchical task tree

**Purpose:** Represent parsed commands in an explainable tree.

**Node types (`TcfgNodeType`):**

| Type | Meaning |
|------|---------|
| `TCFG_NODE_TASK_LIST` | Root container |
| `TCFG_NODE_ACTION` | Semantic command (LIGHT_ON, FAN_ON, NAV_GO...) |
| `TCFG_NODE_OPERATOR` | Composition op (`and`, `or`, `then`, `back-to-back`, …) |

**Example trees — composition operators:**

```
Input: "turn on room light and turn off room fan"

TASK_LIST
  LIGHT_ON(room_light) [smarthome]
  OP(and)              — execute both
  FAN_OFF(room_fan) [smarthome]
```

```
Input: "turn on room light or turn off room fan"

TASK_LIST
  LIGHT_ON(room_light) [smarthome]
  OP(or)               — pick one (exclusive)
  FAN_OFF(room_fan) [smarthome]
```

```
Input: "turn on room light turn off room fan"

TASK_LIST
  LIGHT_ON(room_light) [smarthome]
  OP(back-to-back)     — implicit sequence (no connector word)
  FAN_OFF(room_fan) [smarthome]
```

**Cross-domain example:**

```
Input: "turn on room light and come here"

TASK_LIST
  LIGHT_ON(room_light) [smarthome]
  OP(and)
  COME_HERE() [robot]
```

**Related functions:**

| Function | Role |
|----------|------|
| `allocNode()` | Allocate from fixed node pool |
| `attachChild()` | Link parent → child |
| `getTaskTree()` | Read-only root pointer |
| `printTaskTree()` | Serial debug output |
| `printNode()` | Recursive tree printer |

---

### Step 6 — Command Dependency Analysis (CDA)

**Purpose:** Analyze multi-intent commands for execution safety.

**Module:** `TinyCFGDependency` (`TinyCFGDependency.h` / `.cpp`)

**Checks:**
- Task count and operator dependencies
- Navigation conflicts (two concurrent `NAV_GO` commands)
- Resource overlap warnings
- **`or` groups do not create sequential dependencies** (alternatives are mutually exclusive)

**Operator → CDA behavior:**

| Operator | Dependency recorded? | Notes |
|----------|---------------------|-------|
| `and` | Yes | Both tasks linked by conjunction |
| `or` | No | Alternatives; no `fromTask → toTask` dep |
| `back-to-back` | Yes | Implicit sequence treated like ordered intent |
| `then`, `after`, … | Yes | Explicit temporal/conditional ordering |

**Related functions:**

| Function | Role |
|----------|------|
| `TinyCFGDependency::analyze()` | Build dependency report from task tree |
| `TinyCFGDependency::printReport()` | Print CDA summary |
| `getDependencyReport()` | Access last report from parser |

**Example output:**

```
Tasks: 2  Dependencies: 1  Conflicts: 0
Execution safe: YES
```

---

### Step 7 — Metrics & confidence

**Purpose:** Quantify parse quality for research / tuning.

**Struct:** `TcfgParseStats`

| Field | Meaning |
|-------|---------|
| `tokensTotal` | Tokens after tokenization |
| `tokensConsumed` | Tokens cursor reached |
| `patternMatches` | Successful pattern hits |
| `phoneticHits` | PER recoveries |
| `noiseSkipped` | Garbage tokens skipped |
| `gluedSegments` | DGS sub-words produced |
| `recoveryCount` | Total recovery operations |
| `parseLatencyUs` | Parse time (microseconds) |
| `ramUsedBytes` | Estimated RAM footprint |
| `confidence` | 0.0–1.0 quality score |

**Related function:** `printMetrics(Stream&)`

---

### Step 8 — Semantic action execution

**Purpose:** Dispatch commands to your robot / IoT code with **operator-aware semantics**.

| Operator | Dispatch policy |
|----------|-----------------|
| `and` | Execute **all** actions |
| `or` | Present both alternatives; dispatch **first** by default (exclusive choice) |
| `back-to-back` | Execute **all** actions in order (implicit sequence) |
| `then`, `after`, … | Execute all actions (ordered) |

```cpp
void onAction(uint8_t actionId, const char* arg, uint8_t domain, void* userData) {
    Serial.printf("[%s] %s(%s)\n",
        TinyCFG::domainName(domain),
        TinyCFG::actionName(actionId),
        arg);
    // digitalWrite(), motor control, MQTT publish, etc.
}

parser.setActionHandler(onAction);
parser.parse("turn on kitchen fan");
parser.executeActions();           // simple: walks tree, respects OR groups
parser.printPipeline(Serial);      // full trace: uses dispatchActions() in step 8
```

**Related functions:**

| Function | Role |
|----------|------|
| `setActionHandler()` | Register callback |
| `executeActions()` | Walk task tree; OR groups dispatch first alternative only |
| `dispatchActions(out)` | Pipeline step 8: operator-aware dispatch + Serial trace |
| `actionName()` | `TCFG_ACT_LIGHT_ON` → `"LIGHT_ON"` |
| `domainName()` | `1` → `"smarthome"` |
| `seqOpName()` | `TCFG_SEQ_AND` → `"and"`, `TCFG_SEQ_OR` → `"or"`, `TCFG_SEQ_JUXTA` → `"(back-to-back)"` |

---

## 5. Offline grammar compiler

**Tool:** `tools/tinycfg_compiler.py`

```bash
python tools/tinycfg_compiler.py tinycfg/grammars/smart_world.cfg
# Output: src/grammars/smart_world_grammar.h
```

**Grammar file sections:**

| Section | Purpose |
|---------|---------|
| `[meta]` | Grammar name, version |
| `[slots]` | Location & device lists for slot patterns |
| `[terminals]` | Words + synonyms |
| `[rules]` | CFG structure (TASK_LIST, OP_REST...) |
| `[patterns]` | Command → action mappings with `@domain` `@resource` |

**Generated artifacts in `.h` file:**
- `TCFG_SYNONYMS[]` — word → terminal + phonetic key
- `TCFG_PATTERNS[]` — token sequences + actions + slot masks
- `TCFG_LOCATION_TOKENS[]` — valid `{location}` terminals
- `TCFG_OP_TOKENS[]` — composition operator terminal IDs (`and`, `or`, `then`, …)

- `TCFG_ACTION_NAMES[]` — explainability strings

**Composition terminals in `smart_world.cfg`:**

```ini
and     :
or      : alternatively
then    :
after   :
before  :
if      :
until   :
while   :
```

---

## 6. Extreme test cases (step-by-step walkthrough)

Use Serial Monitor @ **115200** with `examples/TinyCFG_CLI_Test/TinyCFG_CLI_Test.ino`.

---

### Test A — Baseline (clean command)

**Input:**
```
turn on room light
```

| Step | What happens |
|------|----------------|
| Tokenize | `[turn, on, room, light]` |
| Lookup | All exact terminal matches |
| Pattern | `turn on {location} light` → `LIGHT_ON(room_light)` |
| Tree | `TASK_LIST → LIGHT_ON(room_light)` |
| Result | **PARSE OK**, confidence ~100% |

---

### Test B — Multi-intent with operator

**Input:**
```
turn on room light and come here
```

| Step | What happens |
|------|----------------|
| Tokenize | `[turn, on, room, light, and, come, here]` |
| Task 1 | `LIGHT_ON(room_light)` |
| Operator | `OP(and)` |
| Task 2 | `COME_HERE()` |
| Tree | 2 actions, 1 operator |
| CDA | 2 tasks, execution safe |

---

### Test C — Named device slots

**Input:**
```
turn on rakibs room fan
```

| Step | What happens |
|------|----------------|
| Tokenize | `[turn, on, rakibs, room, fan]` |
| Lookup | `rakibs` → UNKNOWN (valid `{name}` slot) |
| Pattern | `turn on {name} {location} fan` |
| Arg build | `{name}_{location}_fan` → `rakibs_room_fan` |
| Action | `FAN_ON(rakibs_room_fan)` |

---

### Test D — STT noise injection

**Input:**
```
TURN $$$ ADJFIJA ON 09309582903 ROOM LIGHT
```

| Step | What happens |
|------|----------------|
| Tokenize | `[$$$ dropped], turn, adjfija, on, 09309582903, room, light` |
| Noise skip | `adjfija` (unknown), `09309582903` (digits) skipped between literals |
| Pattern | Gap-tolerant match: `turn ... on ... room light` |
| Action | `LIGHT_ON(room_light)` |
| Metrics | `noiseSkipped > 0`, confidence < 100% but **PARSE OK** |

**Why it works:** `tryMatchPatternAt()` skips up to 12 noise tokens between expected literals.

---

### Test E — Glued camelCase (no spaces)

**Input:**
```
tTurnOnRoomLight
```

| Step | What happens |
|------|----------------|
| DGS | camelCase → `t turn on room light` |
| Noise | Leading `t` skipped |
| Pattern | `turn on {location} light` |
| Action | `LIGHT_ON(room_light)` |
| Metrics | `gluedSegments > 0` |

---

### Test F — Glued lowercase per word in spaced line

**Input:**
```
120 turnonfan and after this turnoffroomlight
```

| Step | What happens |
|------|----------------|
| Spaced split | `[120, turnonfan, and, after, this, turnoffroomlight]` |
| Inline DGS | `turnonfan` → `turn, on, fan`; `turnoffroomlight` → `turn, off, room, light` |
| Noise | `120` (digits), `this` (filler) skipped |
| Operators | `and` consumed; stray `after` skipped before next task |
| Task 1 | `FAN_ON(default_fan)` |
| Task 2 | `LIGHT_OFF(room_light)` via `OP(and)` |
| Tree | 2 real actions — **not** empty operator-only tree |

**This is the hardest real-world STT case:** mixed noise + glued words + filler phrases + multi-intent.

---

### Test G — Phonetic / fuzzy recovery

**Input:**
```
turn on lite and com here
```

| Step | What happens |
|------|----------------|
| Fuzzy | `lite` → `light` (edit distance) |
| PER/fuzzy | `com` → `come` |
| Result | `LIGHT_ON` + `COME_HERE()` if patterns match |

---

### Test I — Composition operators (and / or / back-to-back)

Three sentences with the **same two actions** but **different operators**:

| Input | Tree operator | Dispatch |
|-------|---------------|----------|
| `turn on room light and turn off room fan` | `OP(and)` | Both executed |
| `turn on room light or turn off room fan` | `OP(or)` | Choice shown; first dispatched |
| `turn on room light turn off room fan` | `OP(back-to-back)` | Both executed sequentially |

**Walkthrough (`and` variant):**

| Step | What happens |
|------|----------------|
| Tokenize | `[turn, on, room, light, and, turn, off, room, fan]` |
| Task 1 | `LIGHT_ON(room_light)` |
| Operator | Explicit `and` consumed → `OP(and)` |
| Task 2 | `FAN_OFF(room_fan)` |
| CDA | 2 tasks, 1 dependency, execution safe |
| Data flow [6] | `LIGHT_ON(room_light) and FAN_OFF(room_fan)` |

**Walkthrough (`or` variant):**

| Step | What happens |
|------|----------------|
| Tokenize | `[turn, on, room, light, or, turn, off, room, fan]` |
| Operator | `or` → `OP(or)` — exclusive choice |
| CDA | No sequential dependency between alternatives |
| Dispatch | `>> OR choice: LIGHT_ON(...) OR FAN_OFF(...) — dispatching first` |

**Walkthrough (back-to-back variant):**

| Step | What happens |
|------|----------------|
| Tokenize | `[turn, on, room, light, turn, off, room, fan]` — no `and`/`or` |
| Operator | `canStartCommandAt()` at second `turn` → `OP(back-to-back)` |
| Semantics | Implicit multi-intent without connector word |

---

### Test H — Should fail

**Input:**
```
random gibberish words
```

| Step | What happens |
|------|----------------|
| Tokenize | `[random, gibberish, words]` — all UNKNOWN |
| Pattern | No pattern matches |
| Result | **PARSE FAILED** (0 action nodes) |

---

## 7. Public API quick reference

### `TinyCFG` class (`TinyCFG.h` / `TinyCFG.cpp`)

| Method | Description |
|--------|-------------|
| `TinyCFG()` | Construct parser with default recovery enabled |
| `parse(text)` | Full pipeline; returns true if ≥1 action parsed |
| `tokenizeOnly(text)` | Tokenize without parsing (debug) |
| `reset()` | Clear tokens, tree, stats |
| `setFuzzyMatch(bool)` | Enable/disable edit-distance recovery |
| `setPhoneticRecovery(bool)` | Enable/disable PER |
| `setErrorRecovery(bool)` | Enable/disable gap/noise skip |
| `setActionHandler(fn, data)` | Register semantic callback |
| `setVerbose(bool)` | Verbose parse logging |
| `getTaskTree()` | Root `TcfgTaskNode*` |
| `getStats()` | `TcfgParseStats` metrics |
| `getDependencyReport()` | Last CDA report |
| `printTokens(out)` | Debug token list |
| `printLexicalDetail(out)` | Per-token lexical roles (step 2/3 detail) |
| `printRecoveryConfig(out)` | Fuzzy / PER / gap settings |
| `printActionsSummary(out)` | Matched patterns + operator hints |
| `printDataFlow(out)` | Stages 0–9 transformation diagram |
| `printPipeline(out)` | Data flow + full 8-step trace |
| `getDataFlowStage(n)` | Stage string (`0`=raw … `9`=dispatch) |
| `printTaskTree(out)` | Print hierarchical tree |
| `printDependencyReport(out)` | Print CDA |
| `printMetrics(out)` | Print latency, RAM, confidence |
| `executeActions()` | Operator-aware handler dispatch |
| `dispatchActions(out)` | Dispatch with Serial trace (pipeline step 8) |
| `getGrammarName()` | e.g. `"smart_world"` |
| `getGrammarVersion()` | e.g. `3` |
| `getPatternCount()` | e.g. `74` |
| `actionName(id)` | Action ID → string |
| `domainName(id)` | Domain ID → string |
| `seqOpName(op)` | Operator enum → string |

### `TinyCFGDependency` class

| Method | Description |
|--------|-------------|
| `analyze(root, report)` | Fill `TcfgDependencyReport` |
| `printReport(report, out)` | Human-readable CDA output |

### Key data structures (`TinyCFGTypes.h`)

| Struct / enum | Purpose |
|---------------|---------|
| `TcfgTaskNode` | Tree node (action, operator, or list) |
| `TcfgParseStats` | Parse metrics |
| `TcfgDependencyReport` | CDA output |
| `TcfgSeqOp` | `and`, `or`, `then`, `after`, `before`, `if`, `until`, `while`, `back-to-back` (`TCFG_SEQ_JUXTA`) |
| `TcfgNodeType` | TASK_LIST, ACTION, OPERATOR |
| `TcfgConflictType` | Resource / navigation / security conflicts |

---

## 8. Domains & actions (smart_world v3)

| Domain | Example actions |
|--------|-----------------|
| smarthome | `LIGHT_ON`, `LIGHT_OFF`, `FAN_ON`, `LOCK`, `UNLOCK`, `CURTAIN_OPEN`, `SCENE` |
| robot | `COME_HERE`, `NAV_GO`, `FOLLOW_ME`, `PATROL`, `NAV_HOME`, `NAV_DOCK`, `STOP` |
| media | `MEDIA_PLAY`, `MEDIA_PAUSE`, `VOLUME_UP`, `MUTE`, `NEXT_TRACK` |
| security | `ALARM_ARM`, `ALARM_DISARM`, `CAMERA_VIEW`, `CAMERA_RECORD` |
| climate | `TEMP_SET`, `TEMP_UP`, `HEAT_ON`, `COOL_ON` |
| timer | `TIMER_SET`, `TIMER_CANCEL` |

---

## 9. How to test (CLI harness)

1. Open `examples/TinyCFG_CLI_Test/TinyCFG_CLI_Test.ino`
2. Upload to ESP32
3. Serial Monitor @ **115200**

| Command | Action |
|---------|--------|
| `help` | Show CLI help |
| `info` | Grammar metadata |
| `parse <sentence>` | Data flow + full 8-step pipeline trace |
| `flow <sentence>` | Data-flow diagram only (stages 0–9) |
| `tokens <sentence>` | Tokenize + partial data flow |
| `test` | Run built-in test suite (**31 cases**) |
| `bench` | Latency benchmark |
| `examples` | Sample commands |
| `fuzzy on` / `fuzzy off` | Toggle fuzzy recovery |
| `phonetic on` / `phonetic off` | Toggle PER |
| `trace on` / `trace off` | Show/hide full pipeline trace |
| *(type sentence directly)* | Same as `parse` |

**Trace output order:** Data flow (0–9) → Pipeline steps 1–8 (input, tokenize, lexical, pattern, tree, CDA, metrics, dispatch).

---

## 10. Integration with ESpeech STT

```cpp
#include <ESpeech.h>
#include <TinyCFG.h>

ESpeech stt(...);
TinyCFG parser;

void setup() {
    parser.setPhoneticRecovery(true);
    parser.setFuzzyMatch(true);
    parser.setActionHandler(onRobotAction);
}

void loop() {
    stt.recordAudio();
    String text = stt.getTranscription();
    if (parser.parse(text)) {
        parser.printTaskTree(Serial);
        parser.executeActions();
    }
}
```

See: `examples/TinyCFG_ESpeech/TinyCFG_ESpeech.ino`

---

## 11. Adding custom commands

1. Edit `tinycfg/grammars/smart_world.cfg` (or create a domain `.cfg`)
2. Add terminals, patterns, slots as needed
3. Recompile:
   ```bash
   python tools/tinycfg_compiler.py tinycfg/grammars/smart_world.cfg
   ```
4. Rebuild Arduino sketch

**Example new pattern:**

```ini
turn on {location} heater => HEAT_ON({location}) @domain climate @resource hvac
```

---

## 12. Novel contributions (conference paper summary)

1. **Pattern-augmented LL(1)** — CFG composition rules + compiled terminal patterns in flash
2. **DGS (De-Glued Segmentation)** — camelCase + dictionary segmentation for STT spacing loss
3. **Gap-tolerant pattern matching** — skips noise tokens between command literals
4. **PER** — Phonetic Error Recovery via Soundex-like synonym keys
5. **Slot-based named entities** — `{name}`, `{location}` without pre-registration
6. **CDA** — Command Dependency Analyzer for multi-intent safety
7. **Multi-domain grammar** — 6 domains, 74 patterns, single composite grammar
8. **Intelligent N-intent composition** — distinct semantics for `and` (both), `or` (exclusive choice), and implicit back-to-back commands
9. **Explainable data-flow trace** — stage-by-stage input→output transformation for embedded debugging

---

## 13. File map

| Path | Role |
|------|------|
| `tinycfg/grammars/smart_world.cfg` | Master grammar definition |
| `tools/tinycfg_compiler.py` | Offline compiler |
| `src/grammars/smart_world_grammar.h` | Generated tables (do not edit) |
| `src/TinyCFG.h` / `TinyCFG.cpp` | Runtime parser |
| `src/TinyCFGTypes.h` | Shared structs & limits |
| `src/TinyCFGDependency.h` / `.cpp` | CDA module |
| `src/TinyCFGGrammar.h` | Grammar header include |
| `examples/TinyCFG_CLI_Test/` | Serial test harness (no STT) |
| `examples/TinyCFG_ESpeech/` | STT + TinyCFG pipeline |
| `tinycfg/README.md` | Quick start |
| `tinycfg/method.md` | This document |

---

## 14. Evaluation checklist (reproducible)

1. Flash `TinyCFG_CLI_Test`
2. Run `test` → record pass rate (target: **30/31+**)
3. Run `bench` → record avg latency (µs)
4. Run extreme cases D, E, F manually → verify task tree
5. Run composition cases (Test I): `and` / `or` / back-to-back → verify operator nodes differ
6. Run `flow <sentence>` → verify data-flow stages 0–9
7. Toggle `fuzzy off` on Test G → compare confidence drop
8. Run multi-intent + CDA cases → verify `executionSafe`
9. Note `sizeof(TinyCFG)` from `info` for memory table in paper

---

*TinyCFG v3 — smart_world grammar (82 terminals, 74 patterns) — ESpeech Library*
