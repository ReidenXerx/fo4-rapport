---
name: gitnexus-area-demo
description: "Skill for the Demo area of fo4-rapport. 14 symbols across 2 files."
---

# Demo

14 symbols | 2 files | Cohesion: 80%

## When to Use

- Working with code in `scripts/`
- Understanding how distance_to, frame, lines work
- Modifying demo-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `scripts/demo/demo.py` | distance_to, frame, lines, run_shot, say (+5) |
| `scripts/demo/record.py` | client, setup, start, stop |

## Entry Points

Start here when exploring this area:

- **`distance_to`** (Function) — `scripts/demo/demo.py:69`
- **`frame`** (Function) — `scripts/demo/demo.py:270`
- **`lines`** (Function) — `scripts/demo/demo.py:50`
- **`run_shot`** (Function) — `scripts/demo/demo.py:137`
- **`say`** (Function) — `scripts/demo/demo.py:108`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `distance_to` | Function | `scripts/demo/demo.py` | 69 |
| `frame` | Function | `scripts/demo/demo.py` | 270 |
| `lines` | Function | `scripts/demo/demo.py` | 50 |
| `run_shot` | Function | `scripts/demo/demo.py` | 137 |
| `say` | Function | `scripts/demo/demo.py` | 108 |
| `send` | Function | `scripts/demo/demo.py` | 54 |
| `wait_for_load` | Function | `scripts/demo/demo.py` | 93 |
| `client` | Function | `scripts/demo/record.py` | 38 |
| `setup` | Function | `scripts/demo/record.py` | 56 |
| `start` | Function | `scripts/demo/record.py` | 91 |
| `stop` | Function | `scripts/demo/record.py` | 101 |
| `game_running` | Function | `scripts/demo/demo.py` | 87 |
| `main` | Function | `scripts/demo/demo.py` | 295 |
| `set_config` | Function | `scripts/demo/demo.py` | 285 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Main → Running` | cross_community | 5 |
| `Main → Say` | cross_community | 3 |
| `Main → Game_running` | intra_community | 3 |

## How to Explore

1. `context({name: "distance_to"})` — see callers and callees
2. `query({search_query: "demo"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
