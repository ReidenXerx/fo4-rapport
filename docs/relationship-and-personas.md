# Relationship and personas — design

Decisions, with the reason, in the style of Chemistry's `DESIGN.md`. A line appears here
once it has been chosen; anything still open is in **Open questions** at the end.

**Status: DESIGN ONLY.** Nothing below is built. This exists so the reasoning survives the
conversation it came from (2026-09-21) rather than being re-derived.

**Where this belongs.** The relationship store is **Rapport's**, which is why the document
lives here: Chemistry wants it, the SEU-replacement wants it, and anything a second mod
would also need belongs in the framework. The decisions prefixed `N-` are the new mod's and
**move to its own repo the day it has one** — they are here only so they are not lost.

The owner's original roadmap, which this is the worked-out form of, is
`Projects/f4-roadmap`.

---

## The layering, restated

    YOUR MODS        Chemistry · SEU-replacement · anything later
                     decide WHO, WHEN and WHY
    RAPPORT          relationship store · persona derivation · scene machinery
    AAF              plays the animation
    ENGINE           the vanilla relationship records, factions, cells

Chemistry reads the store and lets it bias pairing. The new mod reads and *writes* it
through dialogue. Neither owns it.

---

## R-1 - The relationship store lives in Rapport, not in a consumer (2026-09-21)

Owner's decision, and consistent with everything already shipped: Chemistry is policy,
Rapport is mechanism. A relationship number that Chemistry owned would be invisible to the
SEU-replacement, and two mods keeping separate opinions of whether two NPCs are close is
the contradiction `C-6` already cost us once, in a smaller form.

It also makes the store independently valuable: **Chemistry can consume it the week it
exists, even if the dialogue mod never ships.** That is the reason to build it first.

## R-2 - The store is ours; vanilla is an INPUT, not the substrate (2026-09-21)

The engine's own relationship data is read-rich and write-poor, and cannot carry what we
need. Measured, not assumed — see `docs/npc-data-inventory.md` for the full inventory:

- `Actor.GetRelationshipRank(Actor)` / `SetRelationshipRank(Actor, Int)` are genuinely
  **NPC-to-NPC**, not merely NPC-to-player. Both are Papyrus natives.
  (`Actor.psc:144`, `Actor.psc:500`)
- `HasFamilyRelationship(Actor)` and `HasParentRelationship(Actor)` are **free** — no form
  argument. (`Actor.psc:154`, `:160`)
- `Quest.OnStoryRelationshipChange(ref1, ref2, oldRank, newRank)` fires when vanilla changes
  one, so we can follow the game's own lore as it happens. (`Quest.psc:188`)

So we import, we do not replace. A vanilla marriage or family tie becomes a seed value in
our store, and from then on our own arithmetic runs on top. That is what the owner meant by
*"streamline vanilla relationship lore pieces into our own mechanism"*.

## R-3 - Associations must be hardcoded, because they cannot be enumerated (2026-09-21)

`HasAssociation(AssociationType, Actor)` needs an actual `AssociationType` **form**
(`Actor.psc:150`), and `AssociationType.psc` is a **one-line stub with zero functions**. You
can hold one as a property, pass it, and compare it. You cannot list them, read a label off
one, or construct one from a string.

This is the same shape as the problem `C-8` already hit with `Location.HasKeyword`, and it
has the same answer: pick the vanilla associations that matter, hardcode those forms, and
accept that the set is finite. It is not a blocker — it is a config file with a fixed number
of entries.

The `BGSRelationship` record itself is **C++ only** (`TESForms.h:2881`), holds `npc1`,
`npc2` and an `assocType`, and its `packedData` field has **no accessor and no documented
bits**. If the Papyrus surface ever proves too thin, that is where to go — and it is a real
piece of work, not a quick read.

## R-4 - Measure the rank scale before building a curve on it (2026-09-21)

`GetRelationshipRank` returns an `Int` and **nothing in the base scripts or the headers
declares what the integers mean.** Skyrim's convention (Lover / Ally / Friend / Acquaintance
/ Rival / Foe / Enemy / Archnemesis, +4..-4) is the obvious guess and is **not verified for
Fallout 4**.

The owner wants relationship to bias pairing on a **non-linear curve**. A curve fitted to a
guessed scale is a curve that is wrong everywhere. So: read the rank for a set of known
vanilla pairs — a married couple, a parent and child, two hostile factions — with the F4MCP
harness, and write the observed scale down before any arithmetic is designed.

This is cheap now and expensive later, which is the whole argument for doing it first.

## R-5 - A pair record is written on interaction, never on sight (2026-09-21)

A per-pair store is O(n²) in the number of NPCs the player has ever loaded, and Rapport's
co-save already caps itself deliberately: `kMaxActorRecords = 2000`,
`kMaxPairRecords = 4000` (`src/Ledger.cpp:34-35`, *"2000 actor records is 56 KB and 4000
pairs is 64 KB"*).

So a pair gets a record when something **happens** between them — a scene, a dialogue, a
witnessed event — and never merely because both were loaded in the same cell. The ceiling
then bounds a game's worth of real interactions rather than a walk through Diamond City.

## R-6 - Records are cleaned on death, not on a timer (2026-09-21)

Owner's decision, and correct: a dead NPC's rows are pure waste against a hard ceiling.

`Actor.OnDeath` is available remotely via `RegisterForRemoteEvent` (see the events section
of `docs/npc-data-inventory.md`), so the store can drop both the actor row and every pair
row mentioning it at the moment it happens. No sweep, no timer, no startup cost.

**Not** on a timer, because a periodic sweep over a 4000-row table is exactly the kind of
per-pass cost this project keeps refusing, and it would run forever on a save where nobody
is dying.

---

## R-7 - Persona is DERIVED from the form id, never stored (2026-09-21)

The owner asked how to calculate a persona type, and proposed random values for generic
NPCs. **Derive it instead.** Rapport already does exactly this for the six expression
styles:

    // src/Expressions.cpp:215
    return std::format("{}_{}", a_setID, (a_formID % kStyles) + 1);

The same NPC gets the same answer forever, on every machine, with **zero save cost, no
cleanup, and nothing to keep in sync**. Random would have to be *stored* to stay stable,
which puts it back under R-5's ceiling and R-6's cleanup for no benefit.

It also already reads as character rather than as a shuffle — the expression styles proved
that in play, and personas are the same trick applied to a bigger decision.

**Overrides** come from a config keyed by form id, the way `act-overrides.json` already
handles positions a pack tags badly. A named NPC gets an authored persona; everyone else
gets a derived one. **Bias**, if wanted, can come from faction or class before the modulo —
a raider skewing vulgar is one line and costs nothing.

## R-8 - The persona set, and the one that makes the store matter (2026-09-21)

The owner's three, with the fourth added:

| persona | responds to |
| --- | --- |
| **mercantile** | gifts, caps, material offers |
| **romantic** | fancy words, patience, setting |
| **vulgar** | prime, perverse, direct |
| **reticent** | *nothing, the first few times* |

**reticent** is the one worth arguing for. The other three are single-encounter puzzles —
read the type, pick the matching line. Reticent cannot be solved in one conversation, which
is the only persona that makes the **relationship store load-bearing** rather than a number
that goes up in the corner. Without at least one persona that requires history, the store is
decoration.

---

## N-1 - The new mod is downstream, and does not own anything (2026-09-21)

Same rule as Chemistry: it reads the store, it writes to the store through Rapport's API,
and it keeps none of the mechanism. If something it needs looks useful to a third mod, that
thing belongs in Rapport.

Working name: **Overture** — an approach made to somebody, and a musical opening. Alternates
considered: Allure, Repartee, Inclination.

## N-2 - Dialogue is the riskiest part and gets prototyped FIRST (2026-09-21)

**This is the only part of the roadmap that could fail outright**, and it cannot be
discovered from a header.

There is **no `Topic.psc`, no `TopicInfo.psc`, no `VoiceType.psc`** in the base scripts —
checked by direct listing. They exist only as CK-authored forms held as properties, the same
stub problem as `AssociationType` in R-3. The two routes out of Papyrus are:

- `ObjectReference.Say(Topic akTopicToSay, Actor akActorToSpeakAs, Bool abSpeakInPlayersHead,
  ObjectReference akTarget)` — `ObjectReference.psc:535`, needs authored topics
- `ObjectReference.SayCustom(Keyword akKeywordToSay, ...)` — `ObjectReference.psc:537`, keyed
  by **keyword**, which is the more flexible of the two

Everything else in this document is state and arithmetic, which this project has done
harder versions of. Dialogue decides whether any of it is *playable*, and whether the entry
point is dialogue at all. So the order is: **thinnest possible dialogue loop, then the
store, then personas** — because if dialogue cannot carry it, the store's shape changes with
it and building it first would be building it twice.

**Revised the same day by N-5.** Voice is no longer the blocker - ElevenLabs over MCP covers
it. The prototype's job therefore changes: it is no longer *"can this be voiced"* but **"how
many authored lines does one persona-driven exchange actually need, and in how many voice
types"**. That is a counting exercise, and it is still the first thing to do, because the
answer decides whether the mod is fifty lines or five thousand.

## N-5 - Voice is solved; the LINES still have to be pre-authored (2026-09-21)

Owner's decision: **ElevenLabs over MCP**, so voice lines can be generated directly rather
than shipping silent dialogue or scavenging vanilla audio. That removes the biggest single
objection to N-2 - a custom dialogue mod no longer has to sound like a mute.

It does **not** remove the constraint underneath it, and the difference matters.

**Voiced dialogue is named by form id.** Verified in this install:

    Sound/Voice/VaultTecStory.esp/PlayerVoiceFemale01/00003F12_1.fuz
    Sound/Voice/VaultTecStory.esp/PlayerVoiceFemale01/00003F12_1.lip

`<plugin>.esp / <VoiceType> / <TopicInfoFormID>_<n>.fuz`, with a `.lip` beside it for lip
sync. So **the dialogue system plays a line that already exists in the ESP** - it cannot be
handed a sentence at runtime. Generation happens at BUILD time, not in game.

What follows for the design: **personas select which authored line plays; they do not
compose words.** That is a smaller and much more tractable system than it first sounds, and
it should be designed as a selection problem from day one rather than discovered to be one
later.

**The real cost is the multiplier, and it is voice types, not lines.** Every NPC has a
`VoiceType`, and a line generated in the wrong one sounds wrong immediately. Vanilla ships
dozens. So the size of the job is *lines x voice types*, not *lines*. Three ways to cut it,
none chosen yet:

- restrict the mod to **named NPCs** with known voice types
- ship a small set of **generic** voices and accept the mismatch on the rest
- author the **player's** side voiced and leave NPC replies to subtitles

**Unverified, and worth checking before committing:** the AAF creature pack next door ships
`.fuz` files with arbitrary NAMES rather than form ids
(`Sound/Voice/AAF_DR_creature_pack.esp/DR_female/DR_omega_fuck_no.fuz`), each with a `.lip`
beside it. Some route plays named files. If that route also carries subtitles and lip sync
in the dialogue UI, the pre-authored constraint above gets looser. I did not establish which
route that is.

**Also unverified:** whether `.lip` can be generated outside the Creation Kit. The toolchain
at `D:\F4CustomMods` currently holds only `PapyrusBase` - no fuz or lip tooling yet. Audio
without lip sync plays; the mouth simply does not move, which for a flirt mod is a worse
failure than usual.

One practical note rather than a lecture: cloning a recognisable vanilla voice actor is the
kind of thing that attracts moderation, and two of this account's pages have been moderated
already. Generated original voices carry none of that risk.

## N-3 - Place decides which advance is appropriate (2026-09-21)

The owner asked whether location and ownership had anything in them. It does, and it is
nearly free because Chemistry already computes it.

`WhosePlace` (Chemistry `Autonomy.psc`) already yields *one of them owns this cell* / *their
faction owns it* / *nobody's*, at a cost of up to **eight Papyrus natives per pair**, gated
to interiors and snapshotted once per pass precisely because of that cost. **Reuse the
snapshot. Do not add a second pass.**

What it buys:

- **Being let into someone's home is itself the relationship signal.** No new measurement —
  the fact of where the conversation happens already carries meaning.
- **The same line means different things in different rooms.** Vulgar in a market reads as
  harassment; in their own home it reads as intimacy. This couples persona to place for
  free, and is the cheapest depth in the whole design.
- **Trespass is an attitude hit** — which delivers the first piece of the owner's
  "badly treated" idea at no extra cost.
- **Settlement association** gives the player's own settlers a baseline disposition that
  would otherwise have to be invented.

## N-4 - The NPC-action helpers already exist and get EXTRACTED, not written (2026-09-21)

The roadmap asks for `npc_move_to_player`, `npc_say_thing`, `player_say_thing` and
"more and more such ready to use mini things".

**That is F4MCP.** `walk` (TranslateTo with a standoff and a line-of-sight retry), `travel`
(FastTravel), `look`, `lock` (gaze plus camera), `say`, `freeze`/`thaw` are built and have
been exercised in a live game, including the failures: MoveTo onto a loaded actor hangs the
loading screen, `SetAngle` on the player crashes, `SetCameraTarget` ignores its argument,
and `TranslateTo` has no usable completion event.

So the shipping version is an **extraction** of known-good verbs into a Rapport-side Papyrus
API — not a new build, and not a rediscovery of the same five crashes. The dev file-channel
stays dev.

**Owner's correction, and it is right:** F4MCP was built for *testing*, and its verb set is
shaped by what a test harness needs. It is the **seed**, not the finished article. A gameplay
framework will want things the harness never needed - making an actor face another, hand over
an item, play an idle, wait for the player's answer, run a short scripted beat. Extract what
exists because it is proven, then grow it against what the mod actually asks for, rather than
assuming the test verbs are the right vocabulary.

---

## Deliberately deferred

- **Factions.** The owner's own note: *"i dont have any cool ideas here besides standard
  boring - more respect with factions more relationship buff"*. Agreed, and it is already
  partly covered — shared faction is +0.6 in Rapport's scoring. Nothing more until there is
  a reason.
- **Attitudes and reactions** (incest, cheating, being badly treated). The owner explicitly
  wants this built as a stub with no consumer. Keep it that way: an unused system with one
  hook is cheap; an unused system with a policy layer is a maintenance cost.

## Open questions — measure, do not assume

1. **The rank integer scale** (R-4). Unmeasured. Blocks the curve.
2. **Whether a runtime `SetRelationshipRank` is readable from C++.** The inventory found no
   extra-data class for it — `ExtraFactionChanges` is an enum entry in `BSExtraData.h:123`
   with **no class definition anywhere** in CommonLibF4. If our store is C++-side and vanilla
   changes a rank at runtime, we may only see it from Papyrus.
3. **Which actor values persist in a save.** The flags exist (`kCachePermenant`,
   `kDoesNotRecover`); nothing was found reading them. Only matters if AVs are considered as
   a storage fallback — the co-save is the better answer anyway.
4. **Whether AAF's stat layer is worth adopting.** `AAF_API.ChangeRelationshipStat(actor,
   relationActor, statID, value)` and `GetAttraction(attractor, attractee)` both exist
   (`AAF/AAF_API.psc:374`, `:488`) and **no relationship stat is defined anywhere** — AAF
   built the substrate and left it empty. Using it would make our numbers visible to other
   AAF mods; it would also make us dependent on an unfinished layer. Not decided.
5. **Which route plays NAME-based `.fuz` files** (N-5). The AAF creature pack does it; if that
   route carries subtitles and lip sync, dialogue gets much more flexible.
6. **Whether `.lip` can be generated outside the Creation Kit** (N-5). No fuz/lip tooling in
   the toolchain yet.
7. **How many voice types the mod covers** (N-5). This is the number that decides the size of
   the whole job.
8. **Persona bias inputs.** Faction and class are available; whether they should bias the
   derivation at all is a taste question nobody has answered.

## Scope note

This roadmap is four mods: a relationship store, a persona system, a dialogue system, an
NPC-action API, and an attitude system. The **store is the keystone** — Chemistry consumes
it immediately, it is independently valuable, and every other piece reads it.

Build order that de-risks the most per week: **dialogue prototype (N-2) → store (R-1..R-6) →
personas (R-7, R-8) → everything else.** The prototype comes first only because it is the
one thing that can invalidate the rest.
