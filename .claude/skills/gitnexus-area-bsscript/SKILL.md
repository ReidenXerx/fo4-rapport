---
name: gitnexus-area-bsscript
description: "Skill for the BSScript area of fo4-rapport. 14 symbols across 4 files."
---

# BSScript

14 symbols | 4 files | Cohesion: 100%

## When to Use

- Working with code in `extern/`
- Understanding how IsArray, IsComplex, IsComplexTypeArray work
- Modifying bsscript-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | IsArray, IsComplex, IsComplexTypeArray, SetArray |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | IsArray, IsComplex, IsComplexTypeArray, SetArray |
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | copy, operator=, reset |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | copy, operator=, reset |

## Entry Points

Start here when exploring this area:

- **`IsArray`** (Method) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h:82`
- **`IsComplex`** (Method) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h:91`
- **`IsComplexTypeArray`** (Method) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h:101`
- **`SetArray`** (Method) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h:111`
- **`IsArray`** (Method) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h:82`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `IsArray` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 82 |
| `IsComplex` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 91 |
| `IsComplexTypeArray` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 101 |
| `SetArray` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 111 |
| `IsArray` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 82 |
| `IsComplex` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 91 |
| `IsComplexTypeArray` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 101 |
| `SetArray` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 111 |
| `copy` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | 205 |
| `operator=` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | 43 |
| `reset` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | 198 |
| `copy` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | 205 |
| `operator=` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | 43 |
| `reset` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | 198 |

## How to Explore

1. `context({name: "IsArray"})` — see callers and callees
2. `query({search_query: "bsscript"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
