# TinyCFG — Lightweight CFG Voice Command Parser

TinyCFG is a deterministic, grammar-based parsing engine for ESpeech. It turns
STT transcriptions into hierarchical task trees and executable robot actions —
without cloud parsing or ML on the device.

## Architecture

```
[robot.cfg]  --offline-->  tinycfg_compiler.py  -->  robot_grammar.h (compact tables)
                                                              |
Voice -> ESpeech STT -> TinyCFG Tokenizer -> LL(1) Parser -> Task Tree -> Actions
```

### Design Goals (from TinyCFG objectives)

| Goal | Implementation |
|------|----------------|
| Lightweight | Fixed pools, no heap; < 2 KB RAM typical |
| Deterministic | LL(1) recursive-descent, no ML |
| Multi-intent | `and`, `then`, `after`, `before`, `if`, `until` operators |
| Explainable | `printTaskTree()` outputs human-readable trees |
| ESP32-ready | Arduino C++, PROGMEM-friendly tables |

## Quick Start

### 1. Compile a grammar (offline, on PC)

```bash
python tools/tinycfg_compiler.py tinycfg/grammars/robot.cfg
```

Output: `src/grammars/robot_grammar.h`

### 2. Flash standalone example

Open `examples/TinyCFG_Robot/TinyCFG_Robot.ino`, upload, and type:

```
turn on room light and come here
```

Expected task tree:

```
TASK_LIST
  LIGHT_ON(room_light)
  OP(and)
  COME_HERE()
```

### 3. Full STT pipeline

Open `examples/TinyCFG_ESpeech/TinyCFG_ESpeech.ino`, set WiFi credentials,
and send `start` over Serial to record + parse voice commands.

## Writing a Custom Grammar

Edit `tinycfg/grammars/robot.cfg`:

```ini
[terminals]
turn : switch, activate
on   : enable

[rules]
TASK_LIST -> TASK OP_REST
LIGHT_ON  -> turn on OBJECT => LIGHT_ON($OBJECT)
OBJECT    -> room light => room_light
```

Re-run the compiler and rebuild your sketch.

## API

```cpp
#include <TinyCFG.h>

TinyCFG parser;
parser.setActionHandler(myHandler);
parser.parse("turn on room light and come here");
parser.printTaskTree(Serial);
parser.executeActions();
```

## Error Recovery

- **Fuzzy matching**: tolerates minor STT typos (e.g. `lite` → `light`)
- **Token skip**: unknown tokens are skipped when recovery is enabled
- **Confidence score**: `getStats().confidence` reports parse quality
