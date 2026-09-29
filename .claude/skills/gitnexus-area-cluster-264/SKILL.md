---
name: gitnexus-area-cluster-264
description: "Skill for the Cluster_264 area of fo4-rapport. 8 symbols across 2 files."
---

# Cluster_264

8 symbols | 2 files | Cohesion: 86%

## When to Use

- Working with code in `src/`
- Understanding how GetSingleton work
- Modifying cluster_264-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/PapyrusLink.cpp` | CollectMembers, Members, Papyrus_ForeignSceneAnimation, Papyrus_ForeignSceneEnded, Papyrus_ForeignSceneStarted (+2) |
| `src/ForeignScenes.h` | GetSingleton |

## Entry Points

Start here when exploring this area:

- **`GetSingleton`** (Method) — `src/ForeignScenes.h:35`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `GetSingleton` | Method | `src/ForeignScenes.h` | 35 |
| `CollectMembers` | Function | `src/PapyrusLink.cpp` | 116 |
| `Members` | Function | `src/PapyrusLink.cpp` | 173 |
| `Papyrus_ForeignSceneAnimation` | Function | `src/PapyrusLink.cpp` | 252 |
| `Papyrus_ForeignSceneEnded` | Function | `src/PapyrusLink.cpp` | 271 |
| `Papyrus_ForeignSceneStarted` | Function | `src/PapyrusLink.cpp` | 212 |
| `Papyrus_OwnSceneEnded` | Function | `src/PapyrusLink.cpp` | 287 |
| `Text` | Function | `src/PapyrusLink.cpp` | 204 |

## How to Explore

1. `context({name: "GetSingleton"})` — see callers and callees
2. `query({search_query: "cluster_264"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
