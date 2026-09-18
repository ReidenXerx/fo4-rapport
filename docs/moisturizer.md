# Commonwealth Moisturizer — the second aftermath backend

Two mods do the aftermath job on this machine and they share nothing.

| | CumOverlays | Commonwealth Moisturizer |
| --- | --- | --- |
| What it is | a flat LooksMenu texture on the body | worn geometry: a BodySlide-conformed mesh on an armour slot |
| The face | **nothing** | 10 morph stages per sex, conformed to any head by TRI, plus static NPC variants |
| How it is driven | AAF's `ApplyOverlaySet` | its own modder API on `CMkz_SemenLib` |
| What it costs | nothing | a BodySlide build, and an armour slot |

The mesh has actual volume and is the only one of the two that does faces at all, so `"backend":
"auto"` picks it when both are installed. Rapport drives **one** of them and stops that one's own AAF
listener; the other is left completely alone. Two sets of art on one body fight, and silencing a mod
Rapport is not driving would be a change with nothing behind it.

## Why a separate plugin

`Rapport:Moisturizer` lives in `Rapport_Moisturizer.esp`, not in `Rapport.esp`, and that is the
whole design.

Calling Moisturizer's API means naming `CMkz:CMkz_LibScript` in a script. A script that names a type
nobody has installed carries a reference the VM cannot resolve — and if that script were
`Rapport:Bridge`, the one script the entire framework depends on, every install without Moisturizer
would be carrying that risk to gain one integration. So everything that names a CMkz type lives in
one script, in one optional plugin, which only exists on machines that have the mod.

The orders for it therefore travel in their own queue, drained by its own timer, alongside the
bridge's. Same doorbell arrangement, twice, for the same reason as always: the plugin cannot call
into the VM.

Neutralising Moisturizer's own timer did **not** need the separate plugin — those two settings are
`GlobalVariable`s, a vanilla type — but it lives there anyway because it belongs with the rest of the
integration.

## What Rapport takes over, and what it must not

`ComMoisturizer.esp` has exactly two quests, and they split perfectly:

- **`CMkz_EventHandler` (`0x0008DB`) is stopped.** It listens to `OnSceneInit` / `OnSceneEnd` and
  applies cum on its own schedule. With Rapport driving, two things would be deciding.
- **`CMkz_SemenLib` (`0x000F99`) keeps running.** It is the library Rapport calls. Stopping it would
  stop the thing we are trying to use.

Its own removal timer is set to **0 minutes**, which is its own documented value for "no timer" —
the same escape hatch Rapport's overlay sets use by omitting `duration`. Removal then belongs
entirely to Rapport, from the co-save, in game hours. Its default was 5 real minutes, which no save
survives.

## The mapping

Its API takes three places rather than named sets:

```papyrus
ApplyRandCumAtLocations(actor, AddFront, AddOral, AddRear, SemenColor)
ClearAllCumFromActor(actor)
```

`aftermath.json`'s `regions` block translates Rapport's sets into those letters, so the tag mapping
drives both backends instead of being written out twice:

```json
"Rapport_Vaginal": "F",  "Rapport_Oral": "O",  "Rapport_Anal": "R",  "Rapport_DP": "FR"
```

One difference in the marks: on this backend an actor has **one** mark, not one per set, because the
mod has one state per actor and takes it all off in one call. A second scene merges its regions into
the existing mark rather than adding another, or the extra removal would strip what the second scene
had just applied.

## What the player has to do

- **Install it via its FOMOD**, choosing the body that matches theirs — `11 Body CBBE` here.
- **Build the semen outfit in BodySlide** against their own preset, with **Build Morphs** ticked.

  Worth being exact about why, because the obvious objection is a good one: *can it not just morph
  to fit anybody?* It can — that is precisely what Build Morphs buys. LooksMenu morphs a worn mesh
  per actor at runtime (`f4ee.ini`, `[BodyMorph] bEnable=1`, `bEnableModelPreprocessor=1`), and the
  player's own body already works that way: `FemaleBody.nif` sits next to `FemaleBody.tri` in
  CharacterAssets.

  But the morph data has to exist. Moisturizer ships the `.nif` **alone** for every body option
  except AtomicMuscle — CBBE, FusionGirl and BodyTalk3 have no `.tri` at all. So a fresh install has
  a mesh that physically cannot morph, and the BodySlide run is not an alternative to morphing:
  **it is what produces the morph data.**

  And morphs are deltas from the shape a mesh was built to, not absolute fitting. A morph says
  "20% larger than the base this was built against"; if the semen mesh is built to base CBBE and the
  body to a preset, the morph moves both equally and the mismatch survives. The base still has to
  match.

  Rapport checks for the `.tri` at startup and says which of the two situations it is in, because
  the failure looks exactly like the mod being broken.
- **Enable `Rapport_Moisturizer.esp`** alongside `Rapport.esp`.

## Does per-actor morphing buy anything here?

Only if NPCs have individual bodies, and on this machine they do not. There is no
`F4SE/Plugins/F4EE/BodyGen/` folder, so BodyGen is enabled with no templates to randomise from and
every NPC wears the same built body. The 159 XMLs under `Data/DiverseBodiesRedux/BodymorphsFemale`
are **BodySlide presets**, not runtime morphs — an offline library for building a specific body, not
something the game reads.

So one build against one preset currently fits every NPC in the game, and Build Morphs is worth
ticking for what it costs (nothing) rather than for what it changes today.

## What a player actually sees

Confirmed in game 2026-09-18: the facial cum is morphing head geometry and it reads clearly at
conversation distance. It is also, in practice, **most of what anyone sees** — AAF strips clothing
for a scene and the NPC re-equips afterwards, so body cum on a clothed settler is under their
outfit. Nothing is wrong with it; it is simply covered.

That has a design consequence worth stating: the face is the region doing the visible work, which
makes getting ROLES right more valuable than it looks. Putting the oral region on the wrong partner
is currently the most visible thing Rapport can get wrong.

## Known limits

- **`Is3DLoaded` is mandatory.** Unlike an overlay, a worn mesh cannot be applied to somebody who is
  not there. Rapport already waits for a mark's owner to be nearby; if the call is refused anyway the
  order is handed back (`DeferOrder`) and the next tick offers it again, rather than being consumed
  as though something happened.
- **The face on ordinary NPCs is the static mesh**, not the morphing one. Its MCM says the morphing
  version "doesn't work on most vanilla NPCs" without whitelisting them by keyword. Rapport does not
  whitelist anybody — that is a per-NPC decision the player makes in its own MCM.
- **It occupies an armour slot** (51 by default, configurable to 36/58/61 in its MCM), so it can
  conflict with clothing that uses the same one.
- **Roles are still not routed.** Both actors get the same regions, because AAF's tags say what an
  animation was and never to whom. Unchanged by this work, and still the next thing worth fixing.
