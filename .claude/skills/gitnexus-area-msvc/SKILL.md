---
name: gitnexus-area-msvc
description: "Skill for the Msvc area of fo4-rapport. 30 symbols across 4 files."
---

# Msvc

30 symbols | 4 files | Cohesion: 89%

## When to Use

- Working with code in `extern/`
- Understanding how operator<=>, operator==, operator<=> work
- Modifying msvc-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | operator<=>, operator==, get, good, operator* (+8) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | operator<=>, operator==, get, good, operator* (+8) |
| `extern/CommonLibF4/CommonLibF4/include/RE/msvc/functional.h` | good, operator() |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/functional.h` | good, operator() |

## Entry Points

Start here when exploring this area:

- **`operator<=>`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h:698`
- **`operator==`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h:691`
- **`operator<=>`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h:698`
- **`operator==`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h:691`
- **`unique_ptr`** (Class) — `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h:8`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `unique_ptr` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 8 |
| `unique_ptr` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 8 |
| `operator<=>` | Function | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 698 |
| `operator==` | Function | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 691 |
| `operator<=>` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 698 |
| `operator==` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 691 |
| `get` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 393 |
| `good` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 413 |
| `operator*` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 399 |
| `operator->` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 406 |
| `get` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 393 |
| `good` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 413 |
| `operator*` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 399 |
| `operator->` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 406 |
| `operator=` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 331 |
| `release` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 368 |
| `reset` | Method | `extern/CommonLibF4/CommonLibF4/include/RE/msvc/memory.h` | 375 |
| `operator=` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 331 |
| `release` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 368 |
| `reset` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 375 |

## How to Explore

1. `context({name: "operator<=>"})` — see callers and callees
2. `query({search_query: "msvc"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
