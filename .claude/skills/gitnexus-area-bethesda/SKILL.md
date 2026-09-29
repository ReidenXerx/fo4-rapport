---
name: gitnexus-area-bethesda
description: "Skill for the Bethesda area of fo4-rapport. 651 symbols across 125 files."
---

# Bethesda

651 symbols | 125 files | Cohesion: 74%

## When to Use

- Working with code in `extern/`
- Understanding how calloc, free, aligned_alloc work
- Modifying bethesda-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/FormComponents.h` | BSISoundCategory, TESFullName, TESReactionForm, TESTexture, ActorValueOwner (+56) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/TESForms.h` | BGSSoundCategory, TESClass, TESEyes, BGSConstructibleObject, BGSPerk (+34) |
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/TESForms.h` | BGSCollisionLayer, BGSColorForm, BGSKeyword, BGSLensFlare, BGSLocation (+33) |
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/TESBoundObjects.h` | BGSMovableStatic, TESObjectARMA, TESObjectSTAT, BGSNote, TESAmmo (+18) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/TESBoundObjects.h` | BGSMovableStatic, TESObjectARMA, TESObjectSTAT, BGSNote, TESAmmo (+18) |
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSTArray.h` | assign, decay_iterator, insert, operator=, resize (+17) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSTHashMap.h` | clear, do_erase, empty, get_entries, get_entry_for (+16) |
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSTHashMap.h` | clear, empty, operator=, operator=, destroy (+16) |
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/IMenu.h` | InventoryUserUIInterface, ContainerMenuBase, ExamineMenu, GameMenuBase, HolotapeMenu (+13) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSTArray.h` | BSTArray, BSTArray, begin, end, erase (+13) |

## Entry Points

Start here when exploring this area:

- **`calloc`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/MemoryManager.h:289`
- **`free`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/MemoryManager.h:316`
- **`aligned_alloc`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/MemoryManager.h:277`
- **`aligned_free`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/MemoryManager.h:322`
- **`malloc`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/MemoryManager.h:265`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `ActorValueInfo` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/ActorValueInfo.h` | 203 |
| `BGSHeadPart` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BGSHeadPart.h` | 10 |
| `Mod` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BGSMod.h` | 17 |
| `Container` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BGSMod.h` | 102 |
| `Item` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BGSMod.h` | 219 |
| `BGSStoryManagerBranchNode` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BGSStoryManagerTreeForm.h` | 199 |
| `BGSStoryManagerNodeBase` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BGSStoryManagerTreeForm.h` | 179 |
| `BGSStoryManagerTreeForm` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BGSStoryManagerTreeForm.h` | 20 |
| `TESQuest` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BGSStoryManagerTreeForm.h` | 118 |
| `BSISoundCategory` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 301 |
| `TESFullName` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 46 |
| `TESReactionForm` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 60 |
| `TESTexture` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 18 |
| `BSNavmesh` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/NavMesh.h` | 118 |
| `NavMesh` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/NavMesh.h` | 163 |
| `TESFaction` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/TESFaction.h` | 87 |
| `BGSCollisionLayer` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/TESForms.h` | 172 |
| `BGSColorForm` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/TESForms.h` | 173 |
| `BGSKeyword` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/TESForms.h` | 38 |
| `BGSLensFlare` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/TESForms.h` | 186 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `BSTArray → Decay_iterator` | cross_community | 4 |
| `BSTArray → Size` | cross_community | 4 |
| `AddObject → Allocate` | cross_community | 4 |
| `AddObject → GetSingleton` | cross_community | 4 |
| `Operator= → Decay_iterator` | cross_community | 4 |
| `Operator= → Size` | cross_community | 4 |
| `Operator== → Data` | cross_community | 3 |
| `Operator== → Size` | cross_community | 3 |
| `BSTArray → Capacity` | cross_community | 3 |
| `Reserve → Size` | intra_community | 3 |

## How to Explore

1. `context({name: "calloc"})` — see callers and callees
2. `query({search_query: "bethesda"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
