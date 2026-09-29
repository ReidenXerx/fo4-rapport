---
name: gitnexus-area-cluster-238
description: "Skill for the Cluster_238 area of fo4-rapport. 59 symbols across 24 files."
---

# Cluster_238

59 symbols | 24 files | Cohesion: 84%

## When to Use

- Working with code in `src/`
- Understanding how SceneFor, ScenePair, SceneWith work
- Modifying cluster_238-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/PapyrusLink.cpp` | DebugEntry, Papyrus_Pump, Papyrus_RequestScene, TakeoverItem, AbandonInFlight (+11) |
| `src/DebugTriggers.cpp` | Describe, HardRule, Name, NotNow, Refuse (+6) |
| `src/Scheduler.cpp` | FinishPass, OnLoad, PostSlice, Start, ThreadMain |
| `src/Mailbox.cpp` | LocalToPlayer, Run, Start, Watch |
| `src/ActorScan.cpp` | QuestDriving, Step |
| `src/ActorScan.h` | Candidates, ObserverPositions |
| `src/Voices.cpp` | BorrowFor, Speak |
| `src/main.cpp` | MessageHandler |
| `src/AAFHealth.h` | GetSingleton |
| `src/Barks.h` | GetSingleton |

## Entry Points

Start here when exploring this area:

- **`SceneFor`** (Function) — `src/DebugTriggers.cpp:266`
- **`ScenePair`** (Function) — `src/DebugTriggers.cpp:340`
- **`SceneWith`** (Function) — `src/DebugTriggers.cpp:219`
- **`GetSingleton`** (Method) — `src/AAFHealth.h:49`
- **`Step`** (Method) — `src/ActorScan.cpp:111`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `SceneFor` | Function | `src/DebugTriggers.cpp` | 266 |
| `ScenePair` | Function | `src/DebugTriggers.cpp` | 340 |
| `SceneWith` | Function | `src/DebugTriggers.cpp` | 219 |
| `GetSingleton` | Method | `src/AAFHealth.h` | 49 |
| `Step` | Method | `src/ActorScan.cpp` | 111 |
| `Candidates` | Method | `src/ActorScan.h` | 51 |
| `ObserverPositions` | Method | `src/ActorScan.h` | 55 |
| `GetSingleton` | Method | `src/Barks.h` | 21 |
| `GetSingleton` | Method | `src/Config.h` | 164 |
| `GetSingleton` | Method | `src/DebugHub.h` | 32 |
| `GetSingleton` | Method | `src/FaceAuthority.h` | 41 |
| `GetSingleton` | Method | `src/Ledger.h` | 36 |
| `Run` | Method | `src/Mailbox.cpp` | 215 |
| `Start` | Method | `src/Mailbox.cpp` | 112 |
| `Watch` | Method | `src/Mailbox.cpp` | 152 |
| `GetSingleton` | Method | `src/Morphs.h` | 25 |
| `GetSingleton` | Method | `src/Names.h` | 41 |
| `GetSingleton` | Method | `src/Narrator.h` | 24 |
| `AbandonInFlight` | Method | `src/PapyrusLink.cpp` | 1886 |
| `CheckWatchdog` | Method | `src/PapyrusLink.cpp` | 1706 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Start → ChangedSinceLastCheck` | intra_community | 5 |
| `Start → GetSingleton` | intra_community | 5 |
| `Start → GetSingleton` | intra_community | 5 |
| `Start → GetSingleton` | intra_community | 5 |
| `Start → GetSingleton` | intra_community | 4 |
| `Start → GetSingleton` | intra_community | 4 |
| `Start → GetSingleton` | intra_community | 4 |
| `Start → GetSingleton` | intra_community | 4 |
| `Start → GetSingleton` | intra_community | 4 |
| `Begin → GetSingleton` | cross_community | 4 |

## How to Explore

1. `context({name: "SceneFor"})` — see callers and callees
2. `query({search_query: "cluster_238"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
