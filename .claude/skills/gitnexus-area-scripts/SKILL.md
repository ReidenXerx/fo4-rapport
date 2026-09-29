---
name: gitnexus-area-scripts
description: "Skill for the Scripts area of fo4-rapport. 78 symbols across 23 files."
---

# Scripts

78 symbols | 23 files | Cohesion: 95%

## When to Use

- Working with code in `scripts/`
- Understanding how verifyInstall, read_clip, main work
- Modifying scripts-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `scripts/bearing-verify.mjs` | checkModuleDelivery, checkPackageGates, checkRuntimeCoversAgent, checkSkillSymlinks, checkZed (+7) |
| `scripts/bearing-ci.mjs` | blastRadius, collectDiff, detectChanges, num, gn (+4) |
| `scripts/render-barks.py` | _ordinal, _words, norm, numbers_as_words, _post (+3) |
| `scripts/bearing-token-benchmark.mjs` | classicalCost, cypher, gn, graphCost, pickTargets (+1) |
| `scripts/voice-audit.py` | read_clip, decode_voice, discover, main |
| `scripts/voice-similarity.py` | blob_of, build_map, decode_all, main |
| `scripts/voice-races.py` | fields, walk, glob, race_of |
| `scripts/voice-profile.py` | clips_of, main, traits |
| `tools/ba2list.py` | entries, main, read |
| `scripts/bearing-agent.mjs` | currentBranch, git, resolveBaseRef |

## Entry Points

Start here when exploring this area:

- **`verifyInstall`** (Function) — `scripts/bearing-verify.mjs:367`
- **`read_clip`** (Function) — `scripts/voice-audit.py:96`
- **`main`** (Function) — `scripts/voice-inventory.py:62`
- **`voice_types`** (Function) — `scripts/voice-inventory.py:40`
- **`clips_of`** (Function) — `scripts/voice-profile.py:44`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `verifyInstall` | Function | `scripts/bearing-verify.mjs` | 367 |
| `read_clip` | Function | `scripts/voice-audit.py` | 96 |
| `main` | Function | `scripts/voice-inventory.py` | 62 |
| `voice_types` | Function | `scripts/voice-inventory.py` | 40 |
| `clips_of` | Function | `scripts/voice-profile.py` | 44 |
| `main` | Function | `scripts/voice-profile.py` | 163 |
| `traits` | Function | `scripts/voice-profile.py` | 100 |
| `blob_of` | Function | `scripts/voice-similarity.py` | 74 |
| `entries` | Function | `tools/ba2list.py` | 13 |
| `main` | Function | `tools/ba2list.py` | 47 |
| `read` | Function | `tools/ba2list.py` | 40 |
| `main` | Function | `scripts/build-barks-table.py` | 87 |
| `table` | Function | `scripts/build-barks-table.py` | 42 |
| `main` | Function | `scripts/lip-barks.py` | 163 |
| `stamp_of` | Function | `scripts/lip-barks.py` | 105 |
| `main` | Function | `scripts/package-voice.py` | 61 |
| `load` | Function | `tools/make_dialogue.py` | 156 |
| `main` | Function | `scripts/build-lines.py` | 306 |
| `main` | Function | `scripts/build-overture-lines.py` | 236 |
| `add` | Function | `scripts/build-overture-lines.py` | 239 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `One → _words` | cross_community | 5 |
| `Main → Entries` | cross_community | 4 |
| `Main → Read` | cross_community | 4 |
| `Main → Entries` | cross_community | 4 |
| `Main → Read` | cross_community | 4 |
| `Main → Git` | intra_community | 3 |
| `Main → Num` | intra_community | 3 |
| `Main → Gn` | intra_community | 3 |
| `VerifyInstall → ReadStealth` | intra_community | 3 |
| `One → _post` | intra_community | 3 |

## How to Explore

1. `context({name: "verifyInstall"})` — see callers and callees
2. `query({search_query: "scripts"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
