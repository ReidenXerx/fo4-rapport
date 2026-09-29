---
name: gitnexus-area-cluster-268
description: "Skill for the Cluster_268 area of fo4-rapport. 11 symbols across 1 files."
---

# Cluster_268

11 symbols | 1 files | Cohesion: 86%

## When to Use

- Working with code in `src/`
- Understanding how insideScanned, remember, squareOf work
- Modifying cluster_268-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/Placement.cpp` | insideScanned, remember, squareOf, ChooseSpot, PairKey (+6) |

## Entry Points

Start here when exploring this area:

- **`insideScanned`** (Function) — `src/Placement.cpp:558`
- **`remember`** (Function) — `src/Placement.cpp:604`
- **`squareOf`** (Function) — `src/Placement.cpp:548`
- **`ChooseSpot`** (Function) — `src/Placement.cpp:521`
- **`RunSurvey`** (Function) — `src/Placement.cpp:420`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `insideScanned` | Function | `src/Placement.cpp` | 558 |
| `remember` | Function | `src/Placement.cpp` | 604 |
| `squareOf` | Function | `src/Placement.cpp` | 548 |
| `ChooseSpot` | Function | `src/Placement.cpp` | 521 |
| `RunSurvey` | Function | `src/Placement.cpp` | 420 |
| `PairKey` | Function | `src/Placement.cpp` | 75 |
| `RadiusFor` | Function | `src/Placement.cpp` | 88 |
| `Scan` | Function | `src/Placement.cpp` | 186 |
| `Blocking` | Method | `src/Placement.cpp` | 158 |
| `List` | Method | `src/Placement.cpp` | 165 |
| `Worst` | Method | `src/Placement.cpp` | 164 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `RunSurvey → Lower` | intra_community | 3 |

## How to Explore

1. `context({name: "insideScanned"})` — see callers and callees
2. `query({search_query: "cluster_268"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
