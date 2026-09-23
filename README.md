# Rapport

An adult-animation **framework** for Fallout 4: an F4SE plugin plus a minimal Papyrus bridge that
drives [AAF](https://www.nexusmods.com/fallout4/mods/34260) properly, so that mods built on it do
not each have to solve the same problems badly.

It is plumbing. Rapport does not decide when anybody has sex — an addon does, and asks Rapport to
run it. `DESIGN.md` is the specification and outranks this file.

## What it actually gives you

| | |
| --- | --- |
| **Scenes that finish** | AAF ignores the duration you hand `StartScene`. Rapport picks one of the installed packs' own position trees at `StartScene` and lets AAF walk it to the ending its author wrote, with an emergency stop so nobody is ever left flagged busy. |
| **An AAF that is awake** | Roughly half of all save loads leave AAF permanently deaf, with no error anywhere. Rapport detects that and repairs it using AAF's own restart. Verified 3/3, recovering in about four seconds. |
| **Faces** | Across a whole AAF install there are about ten facial-expression references, against 1,358 animations. Rapport ships nine of its own, drives them from what AAF is actually playing, and always takes them off again. |
| **Aftermath that survives a save** | AAF's overlay timer is an in-session countdown: a save, a reload or the game closing mid-count strands an overlay on an NPC forever. Rapport keeps `{actor, set, expiry}` in game hours, in the co-save. |
| **State that is never stranded** | Nothing Rapport applies may be something only Rapport can remove. Busy flags, faces and overlays are all written into the save, cleaned on load, and removable in one switch. |
| **A co-save an addon can read** | Who has been with whom, how many times, how long ago, and how often AAF refused them. Facts only — what "too soon" means is the addon's call. |
| **Relationships and personas** | Every pair that has interacted has a bond from -1 to +1 in the co-save, seeded from the game's own relationships (a married couple starts close). Every NPC has a stable persona -- mercantile, romantic, vulgar or reticent -- and a faithfulness. Addons read and write the store. |
| **Voices and onlookers** | NPCs bark in their persona's voice during a scene, and people nearby notice and comment. |
| **The Narrator** | An optional HUD line when a scene starts, with the real reasons, and the score for those who want it. Addons narrate their own moments through it; your own say "you". |
| **Names for the nameless** (0.2.1) | A Settler or a Drifter an addon introduces gets a first name and a surname, the same ones forever. |
| **Scenes with you** (0.2.1, not yet run in game) | An addon (Overture) can ask for a scene with the player. Rapport holds its one scene slot for your request so autonomy cannot take it, you never bark, and a scene that falls through says so. |
| **Lovers** (0.2.1) | An addon can declare two people lovers, kept apart from the engine's spouses. Falling out ends it. |
| **An addon door** | Two functions: `CanRun(scenario, a, b)` and `RequestScene(a, b, scenario)`, plus the ranked pairs to choose from, the relationship store, and an event when a scene with the player is over (`docs/relationship-api.md`). |

`docs/FEATURES.md` is the full audit, including what is **built but not yet verified in a real
game**. That distinction is kept honestly and is worth reading before relying on anything.

## Requirements

| | |
| --- | --- |
| Game | Fallout 4 **1.10.163** only (old-gen). Any other runtime is refused at load rather than resolving addresses that mean something else. |
| Script extender | F4SE 0.6.23 |
| Scene engine | AAF 1.7.4.1 |
| Faces and overlays | LooksMenu / F4EE — AAF's overlay and morph calls go through it |
| Animation packs | Whatever you already have. Rapport reads their XML rather than requiring a particular pack. |

**Optional, for the aftermath feature only** — one of:

- **CumOverlays** v1.4 — flat LooksMenu textures on the body. There is no face or mouth template
  among its 57, so on that backend the oral region lands on the chest.
- **Commonwealth Moisturizer** — BodySlide-conformed worn geometry with morphing headparts. It does
  faces, which is why `"backend": "auto"` prefers it. Run its FOMOD for your body and build the
  semen outfit in BodySlide with **Build Morphs** ticked; Rapport checks for the resulting `.tri` at
  startup and says which situation you are in, because a mesh with no morph data looks exactly like
  a broken mod.

With neither installed the aftermath feature turns itself off and says so in the log. Nothing else
is affected.

## What it replaces

AAF Autonomy Enhanced and AAF Sex 'Em Up. Rapport stops their quests rather than asking you to
delete anything — the plugins stay in your load order, every form still resolves, no script instance
in your save is left pointing at nothing, and turning the feature off starts them again. Every such
change is named in `Rapport.log` with its reason: automatic, but never secret.

## Install

1. `Rapport.dll` and `Rapport.ini` into `Data/F4SE/Plugins/`.
2. The contents of `data/` into your `Data/` folder — the AAF XML, the JSON config under
   `F4SE/Plugins/Rapport/`, and the generated sweat textures and materials.
3. Enable `Rapport.esp`.
4. **Only if you use Commonwealth Moisturizer:** enable `Rapport_Moisturizer.esp` as well. It is a
   separate plugin on purpose — see "Credits and dependencies".

The sweat textures and materials are generated rather than committed. Run
`python tools/make_overlays.py` before deploying; `scripts/deploy-dev.ps1` warns if you have not.

Settings live in `Data/F4SE/Plugins/Rapport.ini`, read once at startup and documented inline. Two
are worth knowing before you start:

- **`DryRun`** — watch and report, start nothing.
- **`PanicClear`** — set it to 1 and load a save: every overlay Rapport applied comes off, every
  face it set is cleared, the ledger is emptied, and the log reminds you to set it back. **Run this
  before uninstalling, not after.** Once the plugin is gone, nothing knows which overlays were
  Rapport's — they are ordinary LooksMenu overlays and only you, by hand, can remove them.

## Uninstalling

1. Switch the aftermath feature off (or set `PanicClear = 1`) and load your save once, so the quests
   Rapport stopped are started again and everything it applied comes off.
2. Then remove it.

Doing those in the other order silently costs you your cum overlays with nothing to say why.

## How this relates to Chemistry

Two repositories, deliberately:

```
Rapport   (this)      the framework — enumeration, filtering, scoring, the AAF bridge,
                      scene lifecycle, faces, aftermath, the co-save, the addon API
    ^
    |  Core.CanRun / Core.RequestScene / Core.Candidate*
    |
Chemistry (addon)     the policy — which pair, how often, what story fits where
```

Chemistry decides; Rapport does. The split is not tidiness: **Player Proposals** is the next addon
and will want every one of the same primitives. Anything a second mod would also need belongs in
Rapport, because two schedulers would scan the same actors, two co-saves would hold contradictory
cooldowns, and both could select the same NPC with only AAF's `SetActorLocked` preventing a
collision.

Rapport ships a stand-in decision so the framework can be tested with no addon installed. An addon
calls `Core.TakeOverDecisions()` once at startup and that stand-in retires for the session.

## Building

Needs Visual Studio 2022 Build Tools (C++ workload) and [vcpkg](https://github.com/microsoft/vcpkg).

```
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

The result is `Rapport.dll`. Next-gen and VR are off deliberately, so every address resolves to the
one runtime this is tested against.

The Papyrus side needs base sources the Creation Kit would normally install. If you do not have
them, `tools/papyrus_setup.py` reconstructs them from the game's own archives — see
`docs/papyrus-toolchain.md`, which also records the three things reconstruction silently costs you.

`scripts/build-papyrus.ps1` and `scripts/deploy-dev.ps1` wrap the local loop. Both take their paths
as parameters; the defaults are the author's machine and you will want your own.

## Where the numbers come from

Every claim in this repository about how AAF behaves was measured against a running game or read out
of AAF's own files — never from memory or a wiki. **`docs/aaf-under-the-hood.md` is the interesting
one**: 21 numbered findings about AAF's real behaviour, each recording how it was established,
several of which contradict what its API looks like it promises. It is useful to anybody writing an
AAF mod, not only to this one.

- `docs/aaf-api.md` — the call shapes, read out of `AAF_API.pex`'s own debug table and Sex 'Em Up's
  shipped sources.
- `docs/aaf-under-the-hood.md` — the behaviour underneath them, and the traps.
- `docs/two-lifetimes.md` — why the plugin never calls into the Papyrus VM.
- `docs/safeguards.md` — what happens when something goes wrong.
- `docs/aftermath.md`, `docs/moisturizer.md` — the two aftermath backends.
- `docs/papyrus-toolchain.md` — compiling without the Creation Kit.
- `docs/FEATURES.md` — every capability, why it exists, and how it was verified.
- `docs/runs/` — the raw logs behind most of the above.

## Credits and dependencies

**Rapport ships no third-party assets.** The only art in this repository is generated by its own
`tools/make_overlays.py` — three sweat textures and their materials, written because a census of all
16 installed overlay packs found 959 templates and **not one** of them a sweat or a blush.
Everything else Rapport puts on an actor comes from a mod you already installed, driven through that
mod's own API or through AAF's.

| | |
| --- | --- |
| **AAF** — Dagobaking | The scene engine. Rapport is a client of it, not a fork of it, and modifies none of its files. |
| **F4SE** — ianpatt, behippo, purple lunchbox | Including `UI.psc`, which ships with F4SE rather than with the game. |
| **CommonLibF4** — [alandtse's fork](https://github.com/alandtse/CommonLibF4) | OG-only (`ENABLE_FALLOUT_NG/VR=OFF`). Ryan-rsm-McKenzie's original has no `ProcessLists`, no `TES` and no reference enumeration, so loaded actors cannot be enumerated with it at all. |
| **Champollion** — [Orvid](https://github.com/Orvid/Champollion) | Decompiling the base scripts the Creation Kit would have installed. |
| **Commonwealth Moisturizer** | Optional. The only aftermath backend: its cum is worn geometry with headpart swaps, which is why it can put something on a face when a skin overlay cannot. Rapport drives its API, redistributes nothing of it and modifies no file of it — a resource dependency, exactly like an animation pack. Without it, scenes simply leave nothing behind and everything else is unaffected. CumOverlays is no longer a backend; if installed it is silenced so it cannot paint over the meshes. |
| vcpkg | `spdlog`, `nlohmann-json`, `rapidcsv`, `rsm-mmio`. |

## Licence

**GNU General Public License v3.0** — see [`LICENSE`](LICENSE).

In short: use it, study it, change it, share it. Anything you distribute that is derived from this
must carry the same freedoms. `extern/CommonLibF4` is a submodule rather than vendored source and is
governed by its own terms.

## Settled since

- **Visibility and distribution.** Public, and it ships on Nexus:
  [Rapport](https://www.nexusmods.com/fallout4/mods/109219) and
  [Chemistry](https://www.nexusmods.com/fallout4/mods/109225), both age-restricted.
- **Naming.** A-9 settled the name; `DESIGN.md`, `vcpkg.json` and the header comment in
  `Rapport.ini` were swept off the working title "Autonomy Framework" on 2026-09-20.
- **Adult-content gating.** Handled where it is actually enforced -- the Nexus pages carry the
  adult tags, and the hard rules from DESIGN §1 are stated there. Nothing in the repository gates
  anything, because nothing in the repository can.
