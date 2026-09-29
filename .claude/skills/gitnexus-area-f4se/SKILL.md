---
name: gitnexus-area-f4se
description: "Skill for the F4SE area of fo4-rapport. 32 symbols across 6 files."
---

# F4SE

32 symbols | 6 files | Cohesion: 85%

## When to Use

- Working with code in `extern/`
- Understanding how GetPluginName, GetPluginVersion, GetTrampolineInterface work
- Modifying f4se-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | allocate, do_allocate, in_range, write_5branch, write_6branch (+5) |
| `extern/CommonLibF4/CommonLibF4/include/F4SE/Interfaces.h` | EditorVersion, MakeVersion, RuntimeVersion, F4SEVersion, GetProxy (+1) |
| `extern/CommonLibF4/CommonLibF4/src/F4SE/API.cpp` | GetPluginName, GetPluginVersion, GetTrampolineInterface, Init, get |
| `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | EditorVersion, F4SEVersion, GetProxy, MakeVersion, RuntimeVersion |
| `extern/CommonLibF4/CommonLibF4/include/F4SE/Trampoline.h` | allocate, log_stats, release, set_trampoline |
| `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp` | GetTrampolineInterface, get |

## Entry Points

Start here when exploring this area:

- **`GetPluginName`** (Function) — `extern/CommonLibF4/CommonLibF4/src/F4SE/API.cpp:97`
- **`GetPluginVersion`** (Function) — `extern/CommonLibF4/CommonLibF4/src/F4SE/API.cpp:107`
- **`GetTrampolineInterface`** (Function) — `extern/CommonLibF4/CommonLibF4/src/F4SE/API.cpp:176`
- **`Init`** (Function) — `extern/CommonLibF4/CommonLibF4/src/F4SE/API.cpp:59`
- **`GetTrampolineInterface`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp:147`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `GetPluginName` | Function | `extern/CommonLibF4/CommonLibF4/src/F4SE/API.cpp` | 97 |
| `GetPluginVersion` | Function | `extern/CommonLibF4/CommonLibF4/src/F4SE/API.cpp` | 107 |
| `GetTrampolineInterface` | Function | `extern/CommonLibF4/CommonLibF4/src/F4SE/API.cpp` | 176 |
| `Init` | Function | `extern/CommonLibF4/CommonLibF4/src/F4SE/API.cpp` | 59 |
| `GetTrampolineInterface` | Function | `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp` | 147 |
| `EditorVersion` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | 123 |
| `F4SEVersion` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | 124 |
| `GetProxy` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | 106 |
| `RuntimeVersion` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | 128 |
| `allocate` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 108 |
| `do_allocate` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 191 |
| `write_5branch` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 203 |
| `write_6branch` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 259 |
| `allocate` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 116 |
| `create` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 66 |
| `log_stats` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 335 |
| `release` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 337 |
| `set_trampoline` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 90 |
| `allocate` | Method | `extern/CommonLibF4/CommonLibF4/include/F4SE/Trampoline.h` | 57 |
| `log_stats` | Method | `extern/CommonLibF4/CommonLibF4/include/F4SE/Trampoline.h` | 172 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Write_5branch → Release` | cross_community | 4 |
| `Write_5branch → Free_size` | intra_community | 4 |
| `Write_6branch → Release` | cross_community | 4 |
| `Write_6branch → Free_size` | intra_community | 4 |
| `Init → Get` | intra_community | 3 |
| `Write_5branch → Log_stats` | cross_community | 3 |
| `Write_5branch → Relocate` | cross_community | 3 |
| `Write_6branch → Log_stats` | cross_community | 3 |
| `Write_6branch → Relocate` | cross_community | 3 |

## How to Explore

1. `context({name: "GetPluginName"})` — see callers and callees
2. `query({search_query: "f4se"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
