---
name: gitnexus-area-rel
description: "Skill for the REL area of fo4-rapport. 152 symbols across 11 files."
---

# REL

152 symbols | 11 files | Cohesion: 79%

## When to Use

- Working with code in `extern/`
- Understanding how commonPointerRVA, commonReadable, commonResultAllowed work
- Modifying rel-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | is_dependent_resolution, is_id_only_dependency, is_scoped_rip_dependency, is_slot_pattern_dependency, is_unavailable_resolution (+79) |
| `extern/CommonLibF4RD/CommonLibF4/include/REL/Relocation.h` | RelocateIfNewer, RelocateIfNewer, Relocate, Relocate, id_resolve_status_text (+25) |
| `extern/CommonLibF4RD/CommonLibF4/src/REL/Relocation.cpp` | rootOf, logical_function_scopes, module_readable, resolve_callsites, recordFailure (+11) |
| `extern/CommonLibF4/CommonLibF4/include/REL/Relocation.h` | safe_write, address, write, write, write_fill |
| `extern/CommonLibF4/CommonLibF4/include/REL/Pattern.h` | hexacharacters_to_hexadecimal, iterate, rule_for, match |
| `extern/CommonLibF4/CommonLibF4/src/F4SE/Trampoline.cpp` | allocate, write_5branch, write_6branch |
| `extern/CommonLibF4/CommonLibF4/include/REL/ID.h` | id, address, offset |
| `extern/CommonLibF4/CommonLibF4/include/REL/Offset.h` | address, offset, address |
| `extern/CommonLibF4/CommonLibF4/src/REL/IAT.cpp` | GetIATAddr, PatchIAT |
| `src/DialogueVoice.cpp` | Locate |

## Entry Points

Start here when exploring this area:

- **`commonPointerRVA`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:2546`
- **`commonReadable`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:2536`
- **`commonResultAllowed`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:2518`
- **`inSegment`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:2526`
- **`inSegment`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:2839`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `commonPointerRVA` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2546 |
| `commonReadable` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2536 |
| `commonResultAllowed` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2518 |
| `inSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2526 |
| `inSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2839 |
| `inSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3218 |
| `patternMatches` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2558 |
| `resultAllowed` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2831 |
| `resultAllowed` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3210 |
| `rootOf` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2691 |
| `findVTable` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3413 |
| `inSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3249 |
| `pointerRVA` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3229 |
| `readPointer` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3253 |
| `readU32` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3262 |
| `readable` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3238 |
| `resolveChain` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3574 |
| `resolveMsvcRttiNode` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3475 |
| `validMsvcBaseDescriptor` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3308 |
| `validMsvcHierarchy` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3326 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `ResolveChain → Runtime_family_key` | cross_community | 6 |
| `Resolve_candidate → Runtime_family_key` | cross_community | 6 |
| `Lock → Read_le` | intra_community | 5 |
| `ResolveChain → Mapped_record` | cross_community | 5 |
| `StateLock → Readable` | intra_community | 5 |
| `Auto_callsite → Has_ng_id` | cross_community | 5 |
| `Auto_callsite → Has_og_id` | cross_community | 5 |
| `Resolve_candidate → Mapped_record` | cross_community | 5 |
| `Known_mappings → Runtime_family_key` | cross_community | 5 |
| `Resolve_callsites → Module_readable` | intra_community | 4 |

## How to Explore

1. `context({name: "commonPointerRVA"})` — see callers and callees
2. `query({search_query: "rel"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
