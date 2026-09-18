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

**Do not reinstate `ChangePosition`.** Measured: refused 26 times out of 26 — with tags, with a
named position id, and with no filters at all — for content that demonstrably exists, while
`StartScene` matches the same pair constantly. `FindMatchingAnimations` returned 0 for all 17 tags
queried. It does not work; the scene's position is chosen at `StartScene` instead.

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
