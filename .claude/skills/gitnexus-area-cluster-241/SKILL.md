---
name: gitnexus-area-cluster-241
description: "Skill for the Cluster_241 area of fo4-rapport. 9 symbols across 2 files."
---

# Cluster_241

9 symbols | 2 files | Cohesion: 80%

## When to Use

- Working with code in `src/`
- Understanding how sexOf, Apply, OnForeignSceneEnded work
- Modifying cluster_241-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/Aftermath.cpp` | sexOf, Apply, OnForeignSceneEnded, OnSceneEnded, ReceiversOf (+3) |
| `src/Ledger.h` | GameHours |

## Entry Points

Start here when exploring this area:

- **`sexOf`** (Function) — `src/Aftermath.cpp:316`
- **`Apply`** (Method) — `src/Aftermath.cpp:636`
- **`OnForeignSceneEnded`** (Method) — `src/Aftermath.cpp:546`
- **`OnSceneEnded`** (Method) — `src/Aftermath.cpp:395`
- **`ReceiversOf`** (Method) — `src/Aftermath.cpp:306`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `sexOf` | Function | `src/Aftermath.cpp` | 316 |
| `Apply` | Method | `src/Aftermath.cpp` | 636 |
| `OnForeignSceneEnded` | Method | `src/Aftermath.cpp` | 546 |
| `OnSceneEnded` | Method | `src/Aftermath.cpp` | 395 |
| `ReceiversOf` | Method | `src/Aftermath.cpp` | 306 |
| `RegionsFor` | Method | `src/Aftermath.cpp` | 243 |
| `SetsFor` | Method | `src/Aftermath.cpp` | 365 |
| `Tick` | Method | `src/Aftermath.cpp` | 680 |
| `GameHours` | Method | `src/Ledger.h` | 46 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `OnForeignSceneEnded → Lower` | cross_community | 4 |
| `OnForeignSceneEnded → Has` | cross_community | 4 |
| `OnSceneEnded → Lower` | cross_community | 4 |
| `OnSceneEnded → Has` | cross_community | 4 |
| `OnForeignSceneEnded → SexOf` | intra_community | 3 |
| `OnSceneEnded → SexOf` | intra_community | 3 |

## How to Explore

1. `context({name: "sexOf"})` — see callers and callees
2. `query({search_query: "cluster_241"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
