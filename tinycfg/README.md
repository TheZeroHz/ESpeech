# TinyCFG v2 — Voice Command Parser

**TinyCFG** is a lightweight, deterministic Context-Free Grammar (CFG) framework for
multi-intent voice command understanding on resource-constrained ESP32 robots.
It integrates with ESpeech STT but operates **fully offline** during parsing.

## Named Devices & Locations (v3 slots)

TinyCFG v3 introduces **slot patterns** for real-world device naming:

| Slot | Matches | Example |
|------|---------|---------|
| `{location}` | room, kitchen, bedroom, garage... | `kitchen fan` |
| `{name}` | Any user label (unknown word) | `rakibs`, `john`, `sarahs` |
| `{device}` | fan, light, door, curtain... | (in template) |

### Example commands

```
turn on kitchen fan              → FAN_ON(kitchen_fan)
turn on rakibs room fan          → FAN_ON(rakibs_room_fan)
turn on john bedroom light       → LIGHT_ON(john_bedroom_light)
unlock rakibs front door         → UNLOCK(rakibs_front_door)
go to sarahs office              → NAV_GO(sarahs_office)
check garage camera              → CAMERA_VIEW(garage)
open bedroom curtain             → CURTAIN_OPEN(bedroom)
```

Names like `rakibs`, `john`, `sarahs` are captured automatically — no need to pre-register them in the grammar. Apostrophes are stripped (`rakib's` → `rakibs`).

### Define slots in `.cfg`

```ini
[slots]
location = room, bedroom, kitchen, living, garage, office
device = fan, light, lamp, door, curtain

[patterns]
turn on {name} {location} fan => FAN_ON({name}_{location}_fan) @domain smarthome @resource fan
turn on {location} fan        => FAN_ON({location}_fan)        @domain smarthome @resource fan
```

Recompile after editing:
```bash
python tools/tinycfg_compiler.py tinycfg/grammars/smart_world.cfg
```

## Novel Contributions (for paper)

| Contribution | Description |
|---|---|
| **Pattern-Augmented LL(1)** | Dual offline representation: CFG composition rules + compiled terminal patterns |
| **N-Intent Composition** | Formal operators: `and`, `then`, `after`, `before`, `if`, `until`, `while` |
| **PER** | Phonetic Error Recovery using Soundex-like keys on synonym table |
| **CDA** | Command Dependency Analyzer — temporal deps + navigation conflict detection |
| **Multi-Domain Grammar** | 6 domains, 67 patterns, 36 semantic actions in `smart_world.cfg` |

## Architecture

```
[smart_world.cfg] --offline--> tinycfg_compiler.py --> smart_world_grammar.h
                                                          |
Serial/STT text --> Tokenizer --> LL(1) Parser --> Task Tree --> CDA --> Actions
                     PER+fuzzy
```

## Grammars

| File | Domain | Patterns |
|------|--------|----------|
| `tinycfg/grammars/smart_world.cfg` | All domains (composite) | 67 |
| `tinycfg/grammars/smarthome.cfg` | Lights, locks, scenes | 7 |
| `tinycfg/grammars/robot_nav.cfg` | Navigation, patrol, dock | 9 |
| `tinycfg/grammars/robot.cfg` | Legacy minimal robot | 4 |

### Real-world command coverage

- **Smart home**: lights, fan, locks, curtains, scenes (movie/night/party)
- **Robot**: navigate, follow, patrol, come here, dock, stop
- **Media**: play/pause, volume, mute, skip tracks
- **Security**: arm/disarm alarm, camera view/record
- **Climate**: thermostat, heat/cool
- **Timer**: set/cancel countdown

## Compile Grammar (PC)

```bash
python tools/tinycfg_compiler.py tinycfg/grammars/smart_world.cfg
python tools/tinycfg_compiler.py tinycfg/grammars/smarthome.cfg
python tools/tinycfg_compiler.py tinycfg/grammars/robot_nav.cfg
```

## CLI Test (NO STT — CFG only)

Upload **`examples/TinyCFG_CLI_Test/TinyCFG_CLI_Test.ino`** to ESP32.

Open Serial Monitor @ 115200:

```
help
info
test                    # run 19-case test suite
bench                   # latency benchmark
parse turn on room light and come here
tokens go to kitchen then play music
examples
```

### Sample multi-intent commands

```
turn on room light and come here
go to kitchen then play music
arm alarm then patrol garden
scene movie and turn off living light
set timer for ten minutes then dock
unlock back door and open curtain
```

## Evaluation Metrics (built-in)

`parser.printMetrics(Serial)` reports:
- Parse latency (µs)
- RAM footprint (bytes)
- Confidence score
- Phonetic recovery count
- Pattern match count

## API

```cpp
#include <TinyCFG.h>

TinyCFG parser;
parser.setPhoneticRecovery(true);
parser.setActionHandler(handler);
parser.parse("turn on bedroom light and go to kitchen");
parser.printTaskTree(Serial);
parser.printDependencyReport(Serial);
parser.printMetrics(Serial);
parser.executeActions();
```

## Paper Evaluation Checklist

1. Flash `TinyCFG_CLI_Test`, run `test` → record pass rate
2. Run `bench` → record avg/min/max latency
3. Compare `fuzzy on` vs `fuzzy off` on noisy STT strings
4. Compare `phonetic on` vs `off` on phonetic errors (`lite`, `com here`)
5. Run multi-intent commands → verify CDA dependency output
