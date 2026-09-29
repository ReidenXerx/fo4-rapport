---
name: gitnexus-area-cluster-246
description: "Skill for the Cluster_246 area of fo4-rapport. 9 symbols across 1 files."
---

# Cluster_246

9 symbols | 1 files | Cohesion: 96%

## When to Use

- Working with code in `src/`
- Understanding how any, named, pleasure work
- Modifying cluster_246-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/Expressions.cpp` | any, named, pleasure, pleasureFor, LooksLikeSex (+4) |

## Entry Points

Start here when exploring this area:

- **`any`** (Function) — `src/Expressions.cpp:324`
- **`named`** (Function) — `src/Expressions.cpp:452`
- **`pleasure`** (Function) — `src/Expressions.cpp:331`
- **`pleasureFor`** (Function) — `src/Expressions.cpp:273`
- **`FaceForAct`** (Method) — `src/Expressions.cpp:270`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `any` | Function | `src/Expressions.cpp` | 324 |
| `named` | Function | `src/Expressions.cpp` | 452 |
| `pleasure` | Function | `src/Expressions.cpp` | 331 |
| `pleasureFor` | Function | `src/Expressions.cpp` | 273 |
| `FaceForAct` | Method | `src/Expressions.cpp` | 270 |
| `NoteTags` | Method | `src/Expressions.cpp` | 171 |
| `LooksLikeSex` | Function | `src/Expressions.cpp` | 41 |
| `Lower` | Function | `src/Expressions.cpp` | 7 |
| `SplitTags` | Function | `src/Expressions.cpp` | 21 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `FaceForAct → Lower` | intra_community | 3 |

## How to Explore

1. `context({name: "any"})` — see callers and callees
2. `query({search_query: "cluster_246"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
