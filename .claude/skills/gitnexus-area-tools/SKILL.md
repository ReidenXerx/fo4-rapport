---
name: gitnexus-area-tools
description: "Skill for the Tools area of fo4-rapport. 44 symbols across 7 files."
---

# Tools

44 symbols | 7 files | Cohesion: 94%

## When to Use

- Working with code in `tools/`
- Understanding how main, parse, S work
- Modifying tools-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `tools/pex_natives.py` | main, parse, S, flag_names, read_function (+5) |
| `tools/make_mfg.py` | deep_face, pair, pleasure_deep, sym, drift_siblings (+4) |
| `tools/make_dialogue.py` | _field, _record, _zstring, branch, build (+3) |
| `tools/make_esp.py` | build, field, group, main, message_group (+3) |
| `tools/read_esp.py` | parse, i16, u16, u8, ws |
| `tools/make_overlays.py` | main, write_bgem |
| `tools/papyrus_setup.py` | main, user_flags |

## Entry Points

Start here when exploring this area:

- **`main`** (Function) — `tools/pex_natives.py:257`
- **`parse`** (Function) — `tools/pex_natives.py:71`
- **`S`** (Function) — `tools/pex_natives.py:84`
- **`flag_names`** (Function) — `tools/pex_natives.py:114`
- **`read_function`** (Function) — `tools/pex_natives.py:117`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `main` | Function | `tools/pex_natives.py` | 257 |
| `parse` | Function | `tools/pex_natives.py` | 71 |
| `S` | Function | `tools/pex_natives.py` | 84 |
| `flag_names` | Function | `tools/pex_natives.py` | 114 |
| `read_function` | Function | `tools/pex_natives.py` | 117 |
| `branch` | Function | `tools/make_dialogue.py` | 107 |
| `build` | Function | `tools/make_dialogue.py` | 177 |
| `dialogue_quest` | Function | `tools/make_dialogue.py` | 96 |
| `line` | Function | `tools/make_dialogue.py` | 134 |
| `topic` | Function | `tools/make_dialogue.py` | 120 |
| `build` | Function | `tools/make_esp.py` | 139 |
| `field` | Function | `tools/make_esp.py` | 67 |
| `group` | Function | `tools/make_esp.py` | 90 |
| `main` | Function | `tools/make_esp.py` | 183 |
| `message_group` | Function | `tools/make_esp.py` | 100 |
| `quest` | Function | `tools/make_esp.py` | 121 |
| `record` | Function | `tools/make_esp.py` | 82 |
| `zstring` | Function | `tools/make_esp.py` | 73 |
| `parse` | Function | `tools/read_esp.py` | 69 |
| `deep_face` | Function | `tools/make_mfg.py` | 455 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Build → _field` | cross_community | 4 |
| `Build → _record` | cross_community | 4 |
| `Build → _zstring` | cross_community | 4 |
| `Build → _child_group` | cross_community | 3 |
| `Build → Load` | cross_community | 3 |
| `Build → Field` | intra_community | 3 |
| `Build → Record` | intra_community | 3 |
| `Build → Wstring` | intra_community | 3 |
| `Build → Zstring` | intra_community | 3 |
| `Main → _bgem_string` | intra_community | 3 |

## How to Explore

1. `context({name: "main"})` — see callers and callees
2. `query({search_query: "tools"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
