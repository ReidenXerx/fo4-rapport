---
name: gitnexus-area-rex
description: "Skill for the REX area of fo4-rapport. 13 symbols across 1 files."
---

# REX

13 symbols | 1 files | Cohesion: 74%

## When to Use

- Working with code in `extern/`
- Understanding how operator&, operator-, operator<=> work
- Modifying rex-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | operator&, operator-, operator<=>, operator==, operator>> (+8) |

## Entry Points

Start here when exploring this area:

- **`operator&`** (Function) — `extern/CommonLibF4/CommonLibF4/include/REX/REX.h:109`
- **`operator-`** (Function) — `extern/CommonLibF4/CommonLibF4/include/REX/REX.h:137`
- **`operator<=>`** (Function) — `extern/CommonLibF4/CommonLibF4/include/REX/REX.h:105`
- **`operator==`** (Function) — `extern/CommonLibF4/CommonLibF4/include/REX/REX.h:101`
- **`operator>>`** (Function) — `extern/CommonLibF4/CommonLibF4/include/REX/REX.h:151`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `EnumSet` | Class | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 7 |
| `operator&` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 109 |
| `operator-` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 137 |
| `operator<=>` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 105 |
| `operator==` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 101 |
| `operator>>` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 151 |
| `operator^` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 123 |
| `operator|` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 116 |
| `operator+` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 130 |
| `operator<<` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 145 |
| `operator<<` | Function | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 144 |
| `underlying` | Method | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 54 |
| `get` | Method | `extern/CommonLibF4/CommonLibF4/include/REX/REX.h` | 53 |

## How to Explore

1. `context({name: "operator&"})` — see callers and callees
2. `query({search_query: "rex"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
