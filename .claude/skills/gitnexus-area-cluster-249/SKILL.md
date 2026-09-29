---
name: gitnexus-area-cluster-249
description: "Skill for the Cluster_249 area of fo4-rapport. 9 symbols across 7 files."
---

# Cluster_249

9 symbols | 7 files | Cohesion: 94%

## When to Use

- Working with code in `src/`
- Understanding how number, Overlay, Load work
- Modifying cluster_249-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/FaceAuthority.cpp` | number, SendKnobs |
| `src/Barks.cpp` | Load, LoadOverrides |
| `src/McmSettings.h` | Overlay |
| `src/Config.cpp` | LoadScoring |
| `src/Names.cpp` | Load |
| `src/Narrator.cpp` | Load |
| `src/Watchers.cpp` | Load |

## Entry Points

Start here when exploring this area:

- **`number`** (Function) — `src/FaceAuthority.cpp:740`
- **`Overlay`** (Function) — `src/McmSettings.h:21`
- **`Load`** (Method) — `src/Barks.cpp:31`
- **`LoadOverrides`** (Method) — `src/Barks.cpp:106`
- **`LoadScoring`** (Method) — `src/Config.cpp:218`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `number` | Function | `src/FaceAuthority.cpp` | 740 |
| `Overlay` | Function | `src/McmSettings.h` | 21 |
| `Load` | Method | `src/Barks.cpp` | 31 |
| `LoadOverrides` | Method | `src/Barks.cpp` | 106 |
| `LoadScoring` | Method | `src/Config.cpp` | 218 |
| `SendKnobs` | Method | `src/FaceAuthority.cpp` | 721 |
| `Load` | Method | `src/Names.cpp` | 118 |
| `Load` | Method | `src/Narrator.cpp` | 137 |
| `Load` | Method | `src/Watchers.cpp` | 34 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Load → OverridePath` | intra_community | 3 |

## How to Explore

1. `context({name: "number"})` — see callers and callees
2. `query({search_query: "cluster_249"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
