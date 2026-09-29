---
name: gitnexus-area-gfx
description: "Skill for the GFx area of fo4-rapport. 96 symbols across 13 files."
---

# GFx

96 symbols | 13 files | Cohesion: 88%

## When to Use

- Working with code in `extern/`
- Understanding how Value, Value, StateBag work
- Modifying gfx-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | Value, AcquireManagedValue, IsManagedValue, ReleaseManagedValue, Value (+33) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | Value, AcquireManagedValue, IsManagedValue, ReleaseManagedValue, Value (+26) |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | StateBag, State, Translator, GetStateBagImpl, SetState |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | MovieRoot, GASRefCountBase, RefCountBaseGC, VMFile, VM |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | MovieRoot, VM, GASRefCountBase, RefCountBaseGC, VMFile |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | StateBag, State, GetStateBagImpl, SetState |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Resource.h` | FileTypeConstants, Resource |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | RefCountBase |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Render/Render_ThreadCommandQueue.h` | ThreadCommand |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_ASMovieRootBase.h` | ASMovieRootBase |

## Entry Points

Start here when exploring this area:

- **`Value`** (Class) — `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h:147`
- **`Value`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h:146`
- **`StateBag`** (Class) — `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h:146`
- **`MovieDef`** (Class) — `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h:60`
- **`FileTypeConstants`** (Class) — `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Resource.h:20`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Value` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 147 |
| `Value` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 146 |
| `StateBag` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | 146 |
| `MovieDef` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 60 |
| `FileTypeConstants` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Resource.h` | 20 |
| `Resource` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Resource.h` | 8 |
| `StateBag` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | 95 |
| `MovieDef` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 59 |
| `Movie` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 1090 |
| `RefCountBase` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 61 |
| `ThreadCommand` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Render/Render_ThreadCommandQueue.h` | 11 |
| `State` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | 13 |
| `Movie` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 700 |
| `MovieRoot` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | 362 |
| `ASMovieRootBase` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_ASMovieRootBase.h` | 40 |
| `IListener` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 1261 |
| `MovieRoot` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | 362 |
| `State` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | 14 |
| `Translator` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | 72 |
| `LogState` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/GFx/GFx_Log.h` | 19 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `GetMember → GetType` | cross_community | 3 |
| `Operator= → ObjectAddRef` | intra_community | 3 |
| `Operator= → ObjectRelease` | intra_community | 3 |
| `GetMember → GetType` | cross_community | 3 |
| `Operator= → ObjectAddRef` | intra_community | 3 |
| `Operator= → ObjectRelease` | intra_community | 3 |
| `HasMember → GetType` | cross_community | 3 |
| `HasMember → GetType` | cross_community | 3 |
| `SetMember → GetType` | cross_community | 3 |
| `SetMember → GetType` | cross_community | 3 |

## How to Explore

1. `context({name: "Value"})` — see callers and callees
2. `query({search_query: "gfx"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
