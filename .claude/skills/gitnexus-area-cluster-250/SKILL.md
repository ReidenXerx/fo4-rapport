---
name: gitnexus-area-cluster-250
description: "Skill for the Cluster_250 area of fo4-rapport. 31 symbols across 3 files."
---

# Cluster_250

31 symbols | 3 files | Cohesion: 88%

## When to Use

- Working with code in `src/`
- Understanding how FaceForAct, GetSingleton, Send work
- Modifying cluster_250-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/ForeignScenes.cpp` | AllIn, FacesOn, Holds, IdsOf, IsOurMeta (+22) |
| `src/Expressions.h` | FaceForAct, GetSingleton, Send |
| `src/PapyrusLink.cpp` | Papyrus_NoteScenePosition |

## Entry Points

Start here when exploring this area:

- **`FaceForAct`** (Method) — `src/Expressions.h:91`
- **`GetSingleton`** (Method) — `src/Expressions.h:28`
- **`Send`** (Method) — `src/Expressions.h:205`
- **`Animation`** (Method) — `src/ForeignScenes.cpp:344`
- **`Carry`** (Method) — `src/ForeignScenes.cpp:824`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `FaceForAct` | Method | `src/Expressions.h` | 91 |
| `GetSingleton` | Method | `src/Expressions.h` | 28 |
| `Send` | Method | `src/Expressions.h` | 205 |
| `Animation` | Method | `src/ForeignScenes.cpp` | 344 |
| `Carry` | Method | `src/ForeignScenes.cpp` | 824 |
| `EndOfFinished` | Method | `src/ForeignScenes.cpp` | 928 |
| `Ended` | Method | `src/ForeignScenes.cpp` | 501 |
| `EvictElsewhere` | Method | `src/ForeignScenes.cpp` | 850 |
| `FaceNow` | Method | `src/ForeignScenes.cpp` | 685 |
| `FinishedAt` | Method | `src/ForeignScenes.cpp` | 902 |
| `FitsFinished` | Method | `src/ForeignScenes.cpp` | 914 |
| `OwnSceneEnded` | Method | `src/ForeignScenes.cpp` | 673 |
| `Pump` | Method | `src/ForeignScenes.cpp` | 968 |
| `Remember` | Method | `src/ForeignScenes.cpp` | 893 |
| `Retire` | Method | `src/ForeignScenes.cpp` | 788 |
| `RetireAt` | Method | `src/ForeignScenes.cpp` | 875 |
| `Started` | Method | `src/ForeignScenes.cpp` | 183 |
| `Wear` | Method | `src/ForeignScenes.cpp` | 739 |
| `AllIn` | Function | `src/ForeignScenes.cpp` | 117 |
| `FacesOn` | Function | `src/ForeignScenes.cpp` | 171 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Load → GetSingleton` | cross_community | 3 |
| `NoteAnimationAdvanced → FaceForAct` | cross_community | 3 |
| `NoteAnimationAdvanced → GetSingleton` | cross_community | 3 |
| `Pump → FaceForAct` | cross_community | 3 |
| `Pump → GetSingleton` | cross_community | 3 |

## How to Explore

1. `context({name: "FaceForAct"})` — see callers and callees
2. `query({search_query: "cluster_250"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
