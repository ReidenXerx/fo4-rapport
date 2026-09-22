# Rapport — always-on instructions

An F4SE C++ plugin plus a minimal Papyrus bridge, driving AAF (Advanced Animation Framework) for
Fallout 4 1.10.163 (OG, GOG). `DESIGN.md` is the specification and outranks this file.

## Who you are on this project

**Senior game-engine integration engineer** — F4SE/C++ plugins driving a Papyrus script VM across a
thread boundary, third-party framework integration, and save-persisted state correctness.

This is NOT the mod-management/packaging persona from the `vortex-mod-monitor` repo next door. If a
tool hands you that one, it has the wrong project.

## The four rules that cost the most to learn

**1. The plugin may NEVER call into the Papyrus VM.** It crashed the game twice inside
`DispatchMethodCallImpl`: F4SE tasks run on a `BSJobs` job thread, and the VM packs call arguments
through the per-thread scrap heap. The architecture is a **doorbell** — Papyrus polls native
functions, C++ answers. C++ queues `Order`s; the bridge drains them, one per stack via
`CallFunctionNoWait`. Anything that reverses that direction is wrong no matter how well it seems to
work in a test.

**2. `Bridge.psc` has exactly ONE timer** (`kPollTimer = 1`). Every attempt at a second `StartTimer`
killed the function that started it. The next poll is scheduled BEFORE any AAF call, because a stack
that calls into AAF frequently never comes back.

**3. A failed `as` cast in Papyrus assigns None.** Land that on a loop counter and the loop never
increments — one such bug wrote 845,998 lines and 912 MB. Check every `as` inside a loop.

**4. Arrays live in the SAVE.** A struct that changes shape makes the saved array come back as
`None`, not empty. Never assume an array is non-None; a load is a new lifetime for Papyrus but NOT
for the plugin.

## Two lifetimes

Papyrus variables persist in the save. The DLL starts fresh every launch. A save load is a new
lifetime for one and not the other — which is why the plugin owns "have we introduced ourselves
yet?" and the script cannot.

## AAF

`docs/aaf-under-the-hood.md` is the authority on how AAF actually behaves, as opposed to what its
API looks like it promises. Read it before integrating anything new. The headline: AAF has a load
race that leaves it permanently deaf roughly half the time, with no error anywhere, and Rapport
repairs it.

**`ChangePosition` stays out until somebody re-tests it.** Measured: refused 26 times out of 26 —
with tags, with a named position id, and with no filters at all — while `StartScene` matches the
same pair constantly. That refusal is real and still unexplained. The scene's position is chosen at
`StartScene` instead.

**But the reason we gave for it was wrong.** This file used to say `FindMatchingAnimations` returned
0 for all 17 tags queried, and concluded AAF's tag matching does not work. It does work.
Measured 2026-09-22 on AAF 1741 (1.7.4.1 Beta), Magnolia 0002268B + Randall Chase 001D1F49,
through the `query` dev verb: Kissing 28, PenisToVagina 38, Foreplay 39, Hugging 5, Aggressive 9,
Standing 1, Oral 0. Identical with combinedTags "" and "NONE", identical idle and mid-scene, and
identical for "Kissing" and "KISSING" (so tags are case-insensitive, as AAF's author said).

So AAF can see this content, and "AAF cannot see it" is no longer available as an explanation for
anything. Why the original probe measured 0 is UNKNOWN — the tags it sent were real, its exclude
lists were narrow, and neither the combined field nor scene state changes the answer today. Do not
put a new explanation here without measuring it. Two candidates nobody has tested: a different AAF
build at the time, and a malformed actor array built from the in-flight pair.

**Do not modify AAF's XML to work around it.** Rejected: it fights another mod's deliberate design
and changes behaviour for every AAF mod on the install.

## Evidence

A claim from reading rather than running is unverified. This project has repeatedly had a confident
census of AAF's XML contradicted by AAF itself — twice because `<defaults>` inheritance was missed,
once because `animation="Null"` was. When the game and a static count disagree, **the game wins**,
and the count was measuring the wrong thing.

Papyrus logging stays ON during development. Turning it off to be tidy cost a night.

## Build and deploy

```
cmake --build build --config Release          # needs the VS2022 BuildTools cmake, not on PATH
scripts/build-papyrus.ps1                      # against D:\F4CustomMods\PapyrusBase\Source\Base
scripts/deploy-dev.ps1                         # refuses while Fallout4.exe is running
```

Deploy goes to a Vortex staging folder and updates through hardlinks in place. The game must be
closed; Vortex may stay open.

Decompiled base sources have **no default argument values** — pass every argument explicitly.
`tools/pex_natives.py` writes the sources Champollion silently drops (pure-native scripts have no
bytecode, so it emits no file, no error and no quarantine entry).

## Nexus Mods — always use `nexus-tools`

Anything that touches a Nexus page (description, changelog, version, screenshots) goes through
**`Projects/nexus-tools`**, never a hand-rolled browser driver.

```bash
node ../nexus-tools/src/cli.mjs desc-get fallout4 <modId>
node ../nexus-tools/src/cli.mjs desc-set fallout4 <modId> <file.bbcode> --save
node ../nexus-tools/src/cli.mjs shot <url> <out.png>
```

It attaches over CDP to the owner's already-running Brave (port 9222), so the session is reused and
nothing logs in. Own tab, never `document.cookie` or any credential field, and **it never clicks
Publish or Delete** — that stays the owner's click.

**Why this rule exists.** A hand-rolled CDP driver spent four attempts writing a description into a
`display:none` textarea and reported success every time. The editor is **SCEditor**, with three
surfaces and only one that works:

| surface | what happens |
| --- | --- |
| `.bbcode-editor > textarea` | `display:none`, empty on load — writing does nothing |
| the WYSIWYG iframe | BBCode goes in as **literal text**; the form still submits the old value |
| `.sceditor-button-source` | a real textarea of raw BBCode — **the only door** |

Also: `/edit/description` is a **404** (it lives on `/edit/general`), and the editor hydrates well
after `networkidle`. `desc-set` refuses to save unless what the editor holds matches the file.

## Voiceover — read `docs/VOICE-GUIDELINES.md` FIRST

Any work touching TTS, voice types, `.fuz` files or the bark bank follows the
numbered `V-#` rules there. They are measured, not preferences, and two of them
exist because the obvious choice is wrong:

- **`V-1` — never `eleven_v3`.** It paraphrases: 0/3 verbatim against v2's 3/3.
  These lines ship with subtitles, so a model that rewrites them desyncs the
  screen from the audio.
- **`V-9` — an agent may not pick a voice.** Designing, organising and rendering
  are yours; deciding which preview *sounds* right is the owner's, by ear.
  `render-barks.py` skips `"chosen": null` rather than guessing.

## Publishing to Nexus — read `nexus-tools/docs/TRUST-PIPELINE.md` FIRST

**Read `nexus-tools/CLAUDE.md` before starting** (`AGENTS.md` there for other agents): the rules
every Nexus job follows, the badge/diagram generators, and the scars. Private repo:
https://github.com/ReidenXerx/nexus-tools — clone it next to this project if it is missing.

Anything touching a mod page, a release, or the question *"how do I know this is not
malware"* follows the numbered `T-#` rules there. Three of them exist because the obvious
move is wrong:

- **`T-3` — the CI hash does NOT match the shipped file.** Measured: same size, 83% of
  bytes different, because a different MSVC toolset generates different code. Never tell a
  user to verify their download against a build log until the release actually ships the CI
  artifact.
- **`T-4` — a VirusTotal lookup is free; an upload is permanent and public.** Link a scan
  only at zero detections, and renew it per release: every build has a new hash.
- **`T-1` — public source is necessary and not sufficient.** Nobody can tell by reading a
  repo whether the binary on the page came from it.

<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **fo4-rapport** (31649 symbols, 42058 relationships, 174 execution flows).

> Index stale? Run `node .gitnexus/run.cjs analyze --index-only` from the project root — it auto-selects an available runner. No `.gitnexus/run.cjs` yet? Bootstrap with `npx`, `bunx`, or `pnpm dlx` — e.g. `bunx gitnexus@latest analyze` (npm 11 npx crash; #1939).
> On query/context/impact/cypher object results, read staleness.status and branch/lastCommit. Re-analyze only for behind or diverged — current is clone HEAD, not main.

## Always Do

- **MUST run impact before editing.** Use `impact({target: "symbolName", direction: "upstream"})` or `node .gitnexus/run.cjs impact "symbolName" --direction upstream --repo .`; report callers, processes, and risk. Never substitute grep for graph analysis.
- **MUST analyze graph changes before committing.** Use `detect_changes({scope: "all"})` (MCP) or `node .gitnexus/run.cjs detect-changes --scope all --repo .` (CLI fallback). `partial: true` or `truncated: true` is not a clean check — a zero means unseen, not unaffected; re-run it. For regression review: `detect_changes({scope: "compare", base_ref: "master"})` or `node .gitnexus/run.cjs detect-changes --scope compare --base-ref "master" --repo .`.
- MUST warn on HIGH/CRITICAL `risk` pre-edit; never use `riskSharedAxes` to waive a HIGH/CRITICAL `risk` warning. Compare File/symbol: MCP File omits axes; Graph-RAG expands File.
- **MUST treat `risk: UNKNOWN` as unresolved, not as low.** An empty caller set is not evidence the symbol is unused — it can also mean the callers are not resolvable by the index (plain-object property access, dynamic dispatch, cross-language calls). `impact` pairs `UNKNOWN` with a `riskNote` saying so. Confirm with a text search before treating the symbol as safe to change or delete; do not proceed on the strength of a zero.
- **MUST use `query({search_query: "concept"})` for concepts/flows, `context({name: "symbolName"})` for a named symbol, or `impact` for blast radius, on read-only callers, dependencies, imports, or execution flow.** Graph first; text search only for empty/`UNKNOWN`/literals.
- For security review, `explain({target: "fileOrSymbol"})` lists taint findings (source→sink flows; needs `analyze --pdg`).

## Never Do

- NEVER edit a function, class, or method before MCP/CLI impact analysis.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis, and never read `UNKNOWN` as an all-clear — it means the walk could not answer, which is the one verdict that requires confirming by other means.
- NEVER rename symbols with find-and-replace — use `rename` which understands the call graph.
- NEVER commit before MCP/CLI graph change analysis.

## Resources

| Resource | Use for |
| --- | --- |
| `gitnexus://repo/fo4-rapport/context` | Codebase overview, check index freshness |
| `gitnexus://repo/fo4-rapport/clusters` | All functional areas |
| `gitnexus://repo/fo4-rapport/processes` | All execution flows |
| `gitnexus://repo/fo4-rapport/process/{name}` | Step-by-step execution trace |

## CLI

| Task | Read this skill file |
| --- | --- |
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus-cli/SKILL.md` |

<!-- gitnexus:end -->
