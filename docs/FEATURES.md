# Features — what Rapport does, why, and how we know

Every capability in the plugin, the failure that caused it to exist, and the evidence that it
works. Nothing here is a plan: the roadmap is `docs/roadmap.md` and the decisions are `DESIGN.md`.

## How to read the evidence column

The distinction matters more than the feature list does, because this mod puts state on other
people's NPCs and "it should work" is the shape most of these bugs arrived in.

| Tier | What it means |
| --- | --- |
| **VERIFIED IN GAME** | Somebody watched it happen, or a log line from a real session records it. The evidence is named. |
| **BUILT, NOT VERIFIED IN GAME** | The code exists and compiles. Nothing in this repository records it running on a real save. Not a claim that it is broken — a claim that nobody has looked. |
| **TRIED AND REMOVED** | Built, measured, wrong, deleted. Kept here because the measurement is the finding and re-trying it costs a night. |

Run logs live in `docs/runs/`. They are the primary evidence for most of the VERIFIED rows.

---

## 1. The native core

### Scheduler and actor scan — VERIFIED IN GAME

**What.** A timer thread asks the main thread for short slices (`FrameBudgetMs`, default 0.25 ms).
It enumerates the engine's own loaded-actor list within `ScanRadius`, filters, ranks pairs, and does
nothing for `WarmupSeconds` after a load. `src/Scheduler.h`, `src/ActorScan.h`.

**Why.** Measured, not assumed. The reference load order's main Papyrus problem was a per-NPC spell
running a faction scan: Random Overlay Framework appeared in **62–80% of the stuck stacks in each of
four overload events**, the largest dumping 1,094 stacks, and all four landed in the first six
minutes after a load (`DESIGN.md` §2). So: no per-NPC script, no cloak spell, and nothing at all
during the post-load burst.

**Evidence.** `DESIGN.md` A-8, measured on a running game: Sanctuary 75–78 actors and 19–22
candidates at **0.004–0.015 ms** a pass; Diamond City 49–50 actors and 22–24 candidates at
**0.012–0.028 ms**. Roughly 0.2 µs per actor against a 0.25 ms budget, and a pass has never needed a
second slice.

### Filters: adults, races, quest actors — VERIFIED IN GAME

**What.** Cheapest first: children, dead/disabled, combat, range, race allow-list, dialogue, then
quest-driven. `ScanCounters` records *why* each actor was dropped, so a candidate count is provable
rather than asserted (`src/ActorScan.h`).

**Why.** Two hard rules that are not configurable (`DESIGN.md` §1) and one subtlety: alias
membership is the wrong test for "a quest is directing this actor", because settlers sit in
settlement aliases permanently. The test is an alias with *instanced packages* (A-4).

The race list is an **allow**-list of the six human-skeleton races AAF can animate, so an unlisted
race means "no scene" rather than "a scene with the wrong actor" (A-4).

**Evidence.** `IsChild()` reaches the engine's implementation — Diamond City reports `child 2`,
Sanctuary `child 0` because it has no children (A-8). Race resolution is logged per race at startup:
`race allowed: Human (Fallout4.esm 0x00013746 -> 00013746)` and five more
(`docs/runs/2026-09-18-first-complete-chain.log`).

### Pair scoring — VERIFIED IN GAME

**What.** Every non-hostile pair in range is ranked on proximity, shared faction, interior, night,
onlookers and player presence. Curves rather than lines: proximity falls off fast, and the crowd
penalty is `perObserver * (observers - tolerance)^falloff`, flat until the third bystander.
`src/Pairing.h`, weights in `data/F4SE/Plugins/Rapport/scoring.json`.

**Why.** "Believability over frequency" needs a shape, not a threshold. Zero to two onlookers cost
nothing, three costs 0.20, eight costs 2.94 against a best-possible near 2.65 — in this world nobody
is shy, and nobody performs for ten people either.

**Evidence.** The weights are logged as read, every session:
`scoring: range 1536, observers within 900 | proximity 1.50, faction 0.60, interior 0.30, night 0.25, per observer -0.20, player near -0.50`.

### The candidate feed for addons — VERIFIED, but by Chemistry rather than here

**What.** The ranked pairs are published once a tick as **form ids**, with the raw signals behind the
score, and read back through `Rapport:Core.Candidate*` (`src/Candidates.h`, `papyrus/Rapport/Core.psc`).

**Why form ids.** The `RE::Actor*` in a `ScoredPair` is valid only on the tick that produced it. An
addon reads later, on its own timer. A stale id resolves to `None` and is skipped; a stale pointer is
a crash in somebody else's mod.

**Evidence.** Chemistry consumes the feed and logs a full decision table from it, so it demonstrably
works — but that evidence lives in the other repository, not this one.

---

## 2. The bridge

### The doorbell: Papyrus calls in, C++ never calls out — VERIFIED IN GAME

**What.** The plugin leaves a request on the counter; `Rapport:Bridge` polls `TakeRequest()` every
`PollSeconds` (default 3) and collects it. Orders for AAF travel the same way (`TakeOverlayOrder()`),
and the optional Moisturizer plugin drains a second queue (`TakeMoisturizerOrder()`).
`src/PapyrusLink.h`, `src/Orders.h`.

**Why.** It crashed the game **twice** inside `DispatchMethodCallImpl`. F4SE tasks run on a `BSJobs`
job thread and the VM packs call arguments through the per-thread scrap heap. Asking rather than being
told keeps every VM operation on the VM's own thread. This is the architecture, not a preference:
anything that reverses the direction is wrong however well it tests.

### One timer, and `CallFunctionNoWait` for every AAF call — VERIFIED IN GAME

**What.** `Rapport:Bridge` has exactly one Papyrus timer (`kPollTimer = 1`). Every AAF call goes out
on its own stack via `CallFunctionNoWait`, and the next poll is scheduled *before* any AAF call.

**Why.** Two separate measured failures, both written up in `docs/two-lifetimes.md`:

- The poll ran **17 times** and stopped at the exact poll where `BeginRequest` called `StartTimer`
  with a *second* timer id. Three symptoms — dead poll, failsafe never fired, `OnSceneInit` timer
  never fired — one cause.
- A Papyrus stack **does not come back from an AAF call**. Every AAF API function ends in
  `ui.Invoke`. The call delivers; the stack does not continue. Run 3 settles it: the same function on
  two consecutive requests, the one that returned before reaching AAF survived, the one that reached
  AAF did not. Scheduling the next poll first does *not* fix it, which is how the mechanism was
  pinned down — Papyrus will not start a second `OnTimer` while the first is still running, so one
  stuck poll is every poll after it.

**Consequence designed around rather than discovered:** one poll gets through **at most one** AAF
call. A drain budget of eight is a budget of one.

**Evidence.** `docs/runs/2026-09-18-poll-dies-on-second-timer.log`,
`docs/runs/2026-09-18-poll-dies-at-startscene.log`,
`docs/runs/2026-09-18-nowait-fixed-startscene.log`.

### The bridge-alive alarm — VERIFIED IN GAME

**What.** Polls are counted, and a tick that sees no new poll raises an error, not a statistic
(`PapyrusLink::CheckBridgeAlive`).

**Why.** A poll with nothing to do and a poll that never happened write **identical** logs. Two whole
runs were spent reasoning about the expression layer, which was never broken — it was being starved.
The general rule this earned: *any subsystem whose healthy state and broken state produce the same
log needs a counter, not an argument.*

**Evidence.** The alarm firing, verbatim, in `docs/runs/2026-09-18-poll-dies-at-startscene.log`:
`THE BRIDGE HAS STOPPED POLLING. It last answered after 21 poll(s)...`

### AAF event handling: scene id, and refusal detection — VERIFIED IN GAME

**What.** The bridge registers for AAF's custom events and matches every event to its request by
**AAF's own scene id** (`args[3]`), not by "there is only one request in flight".

A refusal arrives down `OnSceneInit` looking like a success. A real init carries **11** arguments; a
refusal carries **4** — error level in `[0]`, message in `[1]`. The bridge checks the count before
believing the event.

**Why.** Treating a refusal as a start is a tight loop: measured at roughly **eight restarts a
second**, every one logging "scene started" (`docs/aaf-api.md`, `docs/aaf-under-the-hood.md` §9).

**Evidence.** Argument layouts captured from a real scene, 2026-09-18 (`docs/aaf-api.md`, "Event
arguments, read off a real scene").

---

## 3. Scenes

### Tree selection at `StartScene` — VERIFIED IN GAME

**What.** Rapport indexes every `positionTree` in `Data/AAF/*.xml` at startup, grades each by its
ending, and names **one position** when the scene starts. AAF then walks that tree to its own ending
on the pack author's timings. `src/TreeIndex.h`, `src/Scenarios.h`.

**Why.** This is the second design. The first assumed a stage could move a running scene with
`ChangePosition`, and it cannot — see §9. `StartScene` honours `settings.position`, and a position may
declare `positionTree="..."`, so **naming a position is choosing a tree** — the only lever that exists
(`docs/aaf-under-the-hood.md` §3, A-25).

Three graded facts feed selection, each of which changed code:

- The scenario's tags are matched against the position the tree **ends on**, not the one it starts on:
  54 of 74 tree-bearing positions here carry different tags at the two ends, so a request for doggy
  was being answered with a facial (A-25).
- `ending` is graded from a branch **name**, and that is a claim about the author's intent rather than
  a fact 27 times out of 61. `climaxTagged` asks the positions instead
  (`docs/aaf-under-the-hood.md` §18).
- A tree with no `time` attributes reports `seconds = 0`, and **zero means unknown, not instant** —
  treating it as a duration would rank those as the shortest thing available, which is wrong in the
  dangerous direction.

**Evidence.** Complete scenes end to end from 2026-09-18 onwards; the per-tree stage counts in
`docs/aaf-under-the-hood.md` §16 were established by counting events across three complete scenes.

### Scenarios as mood, not as requests — VERIFIED IN GAME

**What.** `quickie`, `athome`, `tender` in `data/F4SE/Plugins/Rapport/scenarios.json`. A stage carries
a face and a share of the story and **asks AAF for nothing**. Stage seconds are weights, rescaled onto
the chosen tree's authored length.

**Why.** The stage clock was cutting climaxes off: a stage lands on a tree-bearing position, AAF starts
walking it, and thirty seconds later the next stage takes it away long before `Climax`. And a climax
cannot be requested instead — UAP hides every standalone climax position and nulls its animation
deliberately (`docs/aaf-under-the-hood.md` §4, §5).

**Rapport does not choose a scenario.** An addon names one. The scheduler names one only while standing
in for Chemistry, and that is marked as a stand-in in the code and in `Rapport.ini`
(`StandInScenario`).

**Known content gap, honestly reported rather than hidden:** two women get no tree at all. AAF's packs
ship 24 female/female positions on the reference install and not one declares a `positionTree`. They
play unconstrained, and `Core.CanRun` answers `0` rather than pretending (A-25).

### The emergency stop — VERIFIED IN GAME

**What.** `MaxSceneSeconds = 600`. Not a scene length and not a model of how scenes end: its only job
is that two actors are never left flagged busy in AAF for the rest of the save.

**Why.** AAF ignores the `duration` handed to `StartScene` (`docs/aaf-under-the-hood.md` §8). A scene
requested with `duration = 30.0` was still running three and a half minutes later, `OnSceneEnd` never
arrived, and both actors kept `AAF_ActorBusy` (`docs/aaf-api.md`). 600 is chosen against the installed
content, not picked round: the longest tree here declares 265 s, and at the worst measured
declared-to-actual ratio (1.89×) that is about 501 s.

### The addon door — VERIFIED, but by Chemistry rather than here

`Rapport:Core.CanRun(scenario, first, second)` and `RequestScene(first, second, scenario)`, plus
`Busy()` and `TakeOverDecisions(who)`.

An addon names a **scenario**, because a name is the only unit whose meaning survives a different
install — tags, trees and positions all differ per machine (A-26). `CanRun` runs the **real** selection
ladder, not a cheaper lookalike, because a pre-flight that says yes to something the start then refuses
is worse than having none. It answers worst-to-better, `-1` to `3`, and **only `-1` is a refusal**.

---

## 4. The face

### Facial expressions — VERIFIED IN GAME

**What.** Rapport's own facial sets in `data/AAF/Rapport_mfgSetData.xml`, applied as the story
advances: eight expressions — anticipation, three levels of pleasure, oral, kiss, climax, dazed —
plus the clearing set. Each expression ships in `kStyles` variants (3 today, 25 `mfgSet` definitions
in total), chosen from the actor's form id so an actor keeps theirs for the whole playthrough at
zero storage cost and nothing has to be written anywhere (`src/Expressions.h`).

The generator and the code must agree on that count: `Expressions::kStyles` appends a number to a
set name, and a name nothing defines is a face that silently never appears. `tools/make_mfg.py`
prints every set it writes so the pair can be checked by eye.

**Why.** The stony face is not a bug in AAF, it is a content gap every pack shares. Across the whole
install there are **ten `mfgSet` references against 2,240 actions and 1,358 animations** — roughly
0.4% — and `mfgSet` hangs off `<action>` and `<animation>`, **not off `<position>`**, so a census of
position attributes finds zero and is simply looking in the wrong place
(`docs/aaf-under-the-hood.md` §19).

This layer depends on nobody: a morph id is an index into the engine's own 50-entry facial table, so
there are no textures to ship and no mod whose assets are being borrowed (A-14).

**Which face** is two questions, and conflating them was the bug: the **act** picks the family and the
**stage** picks the level. A stage named "oral" put `Rapport_Oral` on an actor while AAF played
"Impregnate Cowgirl" — observed in game. A stage can no longer name a face.

The classifier reads the pack's own tags first (98.8% of positions on the reference install), the
position's **name** second (to 99.2%), and a hand-written `act-overrides.json` last. The file is small
on purpose: a full hand-built inventory would be 1,131 entries, 99% of them a second copy of data that
ships with the pack and goes stale when the pack updates. `ReportUnclassified()` prints exactly what
needs overriding, from the classifier that actually runs.

**Evidence.** Confirmed by eye 2026-09-18 (commit `ccf86b8`). The progression is logged per step:
`expressions: 5% through - Rapport_Anticipation ... 95% - Rapport_Climax (5 steps, both actors)`.

### The face is always released — VERIFIED IN GAME

**What.** Everyone wearing a Rapport expression is written into the save (`FACE`) and cleared on the
next load. Clearing is its own order kind: it applies `Rapport_Clear` **and** calls `RemoveMFGBlock`
with all fifty morph ids.

**Why.** Expressions are applied with `lock="true"`, which holds a morph against everything else that
would move it — including the face's own idle and dialogue animation. A locked face is a permanently
frozen face, and a save taken mid-scene is exactly when one gets stranded. The clear is belt and braces
because **nothing on the reference machine demonstrates what releases a lock**: every `mfgSet` in every
installed pack sets `lock="true"` and not one ships `lock="false"`. One extra call, no guess.

**Evidence.** `docs/safeguards.md` §2: an NPC photographed after a scene has a normal, neutral face and
has gone back to what she was doing. `RemoveMFGBlock` does release a `lock="true"` morph.

### `DriveFaces = 0` — BUILT, NOT VERIFIED IN GAME

A switch to turn the whole layer off. It exists because the engine's facial idle writes the same morphs
to blink, breathe and talk, and two writers on a locked morph contend continuously. It is also the
diagnostic: with expressions off, anything still glitching is not Rapport (`data/F4SE/Plugins/Rapport.ini`).
No log in this repository records it being used in anger.

### Sweat and heat — VERIFIED IN GAME (and the bug that shaped it)

**What.** Three sweat overlay levels on biped slot 3, from Rapport's own generated textures and
materials, keyed to the chosen face rather than to a clock.

**Why heat only climbs.** The face and the body need **opposite** rules. An expression legitimately
relaxes — a gentler stage gets a gentler face, and that reads correctly. Sweat accumulates, and nothing
about a calmer minute dries anybody off. Heat was first derived from the face and inherited its fall,
producing this in a measured `tender` scene:

```
prelude   intensity 2   Pleasure_2   ->  Heat_2
main      intensity 1   Pleasure_1   ->  Heat_1     <-- less sweaty, halfway through
finish    intensity 3   Climax       ->  Heat_3
```

`_heatLevel` is now a high-water mark for the scene, reset when the afterglow ends
(`src/Expressions.h`; `docs/aaf-under-the-hood.md`, "Heat climbs and never falls").

### The blush — TRIED AND REMOVED

**What it was.** Three blush templates on biped slot 0 (head), built, shipped, and watched in game
across several scenes.

**What happened.** The faces were **unchanged**. F4EE does not apply a head overlay.

**Why it is worth keeping.** The census predicted it — across all 16 overlay packs installed, 959
templates: 925 on biped slot 3 (body), 34 on slot 4 (left hand), **0 on the head**, and **0** matching
`sweat`/`blush`/`perspir`. But a census can only say "nobody does this", never "it cannot be done", so
it took one launch to settle. The blush was removed rather than left dormant.

A facial flush needs one of the two routes that do reach a face: the CharGen tint layer, or worn
geometry with headpart swaps — which is how Commonwealth Moisturizer does it. Neither is an overlay.
Sweat was never affected; it is slot 3 and works. (`docs/aaf-under-the-hood.md` §21.)

---

## 5. Aftermath

### Persistence in game hours — VERIFIED IN GAME

**What.** `{formID, expiresAt, setID}` per standing mark, in **game hours**, in the co-save. A tick
removes what has expired and applies what has not been asked for yet this session. Default 12 hours.
`src/Aftermath.h`.

**Why.** AAF can already apply an overlay on a timer, and the timer is the whole problem: it is an
**in-session countdown** that does not survive a save, a reload or the game closing mid-count, and an
overlay stranded that way stays on the actor forever with nothing in any log explaining it. Rapport's
own sets therefore carry **no `duration`** — AAF applies and never removes — and removal is Rapport's
(`docs/aftermath.md`, A-13).

Game hours rather than real time: a player who sleeps eight hours should not find it unchanged, and
one who stands still for ten minutes should.

**Why removal and application are the same line of code.** The plugin starts from nothing every launch
and has no memory of having asked. Removal goes out regardless; application waits until the owner is
loaded, because asking AAF about somebody three cells away is a call that can neither be seen to work
nor to fail.

**Evidence.** `aftermath: keep Moisturizer [OF] until hour 792.0`, and the ledger being read back on a
later session. `docs/moisturizer.md` states plainly why "the cum is still there after a reload" does
**not** on its own prove the co-save works — it would look identical with an empty ledger, and then
nothing would know to remove it. The line that proves it is
`ledger: read N standing overlay(s) from the save`.

### The Moisturizer backend — VERIFIED IN GAME

**What.** Commonwealth Moisturizer driven through its own `CMkz_SemenLib` API: worn geometry on an
armour slot, and morphing headparts for the face. Region letters `F`/`O`/`R`, translated from the same
tag mapping that drives the overlay backend.

**Why it is preferred.** It is the only thing on the machine that does **faces** at all, and the face
is in practice most of what anyone sees — AAF strips clothing for a scene and the NPC re-equips
afterwards, so body cum on a clothed settler is under their outfit.

**Why it lives in a second ESP.** Naming `CMkz:CMkz_LibScript` in a script carries a reference the VM
cannot resolve on any install without that mod. Putting that inside `Rapport:Bridge` — the one script
the whole framework depends on — would risk every install to gain one integration. So every CMkz
reference lives in `Rapport:Moisturizer`, in `Rapport_Moisturizer.esp`, with its own order queue and
its own timer (A-19). **This is the rule for every future integration, not a one-off.**

**Why a reload must not re-apply it.** The two backends need opposite treatment on load. An overlay is
runtime LooksMenu state and asking twice is free — AAF will not apply the same overlay to the same
actor twice. A Moisturizer mark is an equipped armour piece with ActorValues recording which slots are
used, which comes back with the save by itself, and `PickRandomFromAVArray` *skips* slots already set
and fills new ones. **Measured before the fix: a reload turned `x3` into `x6`.**

**Evidence.** Confirmed in game 2026-09-18: the facial cum is morphing head geometry and reads clearly
at conversation distance (`docs/moisturizer.md`). Log: `moisturizer: applied front=True oral=True rear=False`.

### The BodySlide check — VERIFIED IN GAME

Moisturizer ships the `.nif` alone for CBBE, FusionGirl and BodyTalk3 — **no `.tri`** — so a fresh
install has a mesh that physically cannot morph, and that looks exactly like the mod being broken.
Rapport checks for the `.tri` at startup and says which of the two situations it is in.

Evidence, verbatim from a run: `aftermath: kzSemen_Female.nif has morph data - it will follow each actor's body`.

### The CumOverlays backend — BUILT, NOT VERIFIED IN GAME

**What.** Six sets in `data/AAF/Rapport_overlayData.xml` (`Rapport_Vaginal`, `_Anal`, `_Oral`,
`_Back`, `_Body`, `_DP`), each carrying `isFemale="true"` and `isFemale="false"` conditions so one set
id resolves correctly for whoever it lands on — nothing in the code needs to know an actor's sex.
`quantity` is 2–3 rather than 1, because one overlay reads as a smudge and three reads as the thing
that was asked for.

**Why it is unverified, stated plainly.** `"backend": "auto"` prefers Moisturizer when both are
installed, and the reference machine has both. Every run log in `docs/runs/` reports
`aftermath: using Commonwealth Moisturizer (worn meshes)` and every health line reports
`0 standing overlay(s)`. **Nothing in this repository records the overlay path applying, expiring or
being removed on a real save.** It is the largest untested surface in the mod, and it is the path a
player with only CumOverlays installed would take.

Known limitation independent of that: there is **no face or mouth template among CumOverlays' 57**, so
on that backend `Rapport_Oral` lands on the chest. That is CumOverlays' limit, not Rapport's.

### Who it lands on — VERIFIED IN GAME (mixed pairs)

**What.** The receiving actor only, for mixed pairs. Both actors for same-sex pairs.

**Why.** The owner asked for the passive role after seeing it on both — a Diamond City guard had it on
his neck after being on the giving end. AAF's own `role` attribute is **dead**: zero occurrences across
every installed pack. What packs do populate is per-actor `gender` (760 F, 1260 M), and every act tag
is `<giver part>To<receiver part>` — so the receiver is read from the tag plus the pair's sexes. That
covers **559 of the 562** two-actor animations that pair `('F','M')` (A-21).

**Same-sex pairs get both, deliberately.** Nothing available separates them: `role` is dead, sex says
nothing, and the slot order that would settle it (slot 0 is the receiving role 559 times against 3)
arrives as a `Var` holding a packed array that Papyrus refuses to unpack, through an F4SE addition to
`Utility` that the vanilla `Utility.pex` does not carry. Applying to neither leaves two people wrong;
applying to both leaves one. The regions are still only the ones the scene's tags actually named.

**Why this matters more than it looks:** on a clothed NPC the face is the only region anyone sees, so
putting the oral region on the wrong partner is the most visible mistake this framework can make.

### The last act decides — VERIFIED IN GAME

Tags accumulate across a scene, but only the tags of the **last animation that named an act** decide
anything. A scene that drifts from vaginal into a blowjob finishes on the blowjob; a kiss or a
transition playing afterwards does not erase it, because only tags that resolve to sets replace the
last act — everything else is still recorded, so "no animation told us anything" stays distinguishable
from "nothing it told us was an act" (A-22).

**Evidence.** The first complete chain did this by accident rather than by test: the scene began on
`PenisToVagina`, drifted into an Atomic Lust blowjob tagged `PenisToMouth`, and resolved `[OF]`.

### The tag mapping is audited, not guessed — VERIFIED (by tool)

`tools/tagaudit.py` compares `aftermath.json`'s rules against every tag the installed packs actually
use. Coverage went **30 → 52 of the 58 act-like tags** in one pass and it found real holes:
`PenisToFace` produced nothing at all, `Handjob` (63 uses) contradicted `HandToPenis` which was already
mapped, `Footjob` (29) contradicted `FootToPenis`, and a `DP` rule matched nothing any installed pack
uses (A-23).

Six tags are deliberately unmapped: `Climax`/`ClimaxM` are markers that name no body part, and
`MouthToMouth`, `MouthToFoot`, `MouthToArmpit` involve no orifice.

**Run the audit after installing any animation pack.** A tag with no rule is indistinguishable in the
log from a scene that was only kissing — both come out as "nothing to leave behind", and the script is
the only thing that separates them.

---

## 6. State that outlives the scene

### The co-save ledger — VERIFIED IN GAME

**What.** Per actor: `{lastSceneAt, lastRefusedAt, lastPartner, scenes, refusals, need}` in game hours.
Per pair: `{lastSceneAt, scenes}`, order-independent. Plus `SCNE`, `FACE` and `OVRL` records.
`src/Ledger.h`.

**Why in the save rather than in a file beside the mod.** A player with three characters has three sets
of these facts, and a file next to the plugin would mix them.

**Why the pair table exists separately.** `lastPartner` holds only the *most recent* partner, so "have
these two ever" becomes unanswerable the moment either is with somebody else — which is exactly when a
settlement becomes interesting. A repeat-pairing bonus is impossible without it.

**Why it records facts and not policy.** "Too soon", "bored of this partner" and "wants company" are an
addon's judgements. The framework guarantees the facts survive a save (A-12). Only a scene that
**ended** is written: a failed request says nothing about two people beyond "not now", and a watchdog
firing means we do not know what happened at all — a guess written into a save outlives the session
that made it.

**Serialization discipline**, taken straight from a mod that hung a load (Mod Switch Framework 1.3.0
wrote a field its loader did not read and looped billions of times, `DESIGN.md` §2): one shared
versioned serializer, every field read checked against the record's remaining length, counts
sanity-capped, unknown versions skipped rather than guessed, unresolvable form ids dropped. Every
on-disk struct is four-byte fields only with a `static_assert` on its size, so the record's length **is**
its arithmetic — which is what makes the length check meaningful.

**Evidence.** `ledger: 00002F0B and 000F61B6 have now had 1 and 1 scene(s), at hour 780.0`, then on a
later pass `2 candidate(s) within the 24-hour cooldown` — the ledger being **read**, not merely written.

### Pruning and bounds — BUILT, NOT VERIFIED IN GAME

`PruneHours` (720 game hours) drops an actor whose last scene and last refusal are both older than that
and who has no overlay standing. Anyone still wearing something keeps their record whatever its age —
the overlay outlives the memory of how it got there, and dropping the record would not remove the
overlay. A hard ceiling sits behind it as a backstop, dropping oldest first.

A 200-hour playthrough is what this is for, and no session in this repository is anywhere near long
enough to have exercised it.

### The four safeguards — VERIFIED IN GAME (two of four)

The rule: **nothing Rapport applies may ever be something only Rapport can remove**
(`docs/safeguards.md`, A-15).

| | Covers | Tier |
| --- | --- | --- |
| `SCNE` | A save written mid-scene. Two form ids; a non-zero pair on load means both are released and the log says so in as many words. | VERIFIED — this is how one NPC became permanently unusable during development, and it took a session to work out |
| `FACE` | A locked expression stranded on somebody. Empty almost always, non-empty exactly during a scene. | VERIFIED 2026-09-18 |
| NaN expiry | A saved expiry that is not finite is set to zero, expiring it immediately. An expiry that is NaN compares false against every `>=`, so the overlay would never expire — the exact outcome the feature exists to prevent, arrived at from the other direction. | BUILT, NOT VERIFIED |
| `PanicClear` | The player's way out: every overlay off, every face cleared, ledger emptied, on the first tick after a load. | BUILT, NOT VERIFIED |

`PanicClear` runs on the first tick after a load rather than at startup, because the ledger is empty
until the save has been read and you cannot take off what you do not know is on. **Run it before
uninstalling, not after** — once the plugin is gone, nothing knows which overlays were Rapport's; they
are ordinary LooksMenu overlays and only the player, by hand, can remove them.

**What is deliberately not covered.** Actors flagged busy by something that is not us. AAF exposes no
way to ask whether a scene is running — `GetAAFStatus` is a readiness flag, not a count — so clearing
another mod's reservation mid-scene would break it, and the framework does not guess. Likewise,
CumOverlays' own earlier applications are not in the ledger, so `PanicClear` will not touch them.

### Stale busy-flag release — BUILT, NOT VERIFIED IN GAME

`StaleFlagGraceSeconds` (120). `AAF_ActorBusy` is stamped by `StartScene` and cleared only by a scene
ending, so a request that died leaves an NPC refused by *every* AAF mod for the rest of the save.
Rapport removes such a flag — but only when no scene is running anywhere, and only after listening long
enough that a scene begun before we connected could not still be going. `0` disables it: never touch a
flag we did not set.

The condition it exists for is real and present: an NPC on the reference install is in exactly that
state, not from anything Rapport did (Chemistry `DESIGN.md` C-7). Nothing here records the release
itself succeeding.

### Refusal backoff — VERIFIED IN GAME

`BusyBackoffSeconds` (300). An actor AAF refuses as busy is benched rather than offered again on the
next tick. Johnny Friendly burned four consecutive ticks before this existed
(`docs/safeguards.md`).

---

## 7. Managing the neighbours

### Takeover — VERIFIED IN GAME

**What.** Rapport stops the quests of mods doing a job it has taken over, and starts them again when it
gives the job back. Four entries today: CumOverlays (2 quests), Commonwealth Moisturizer's event
handler (1), Sex 'Em Up (9), Autonomy Enhanced (9). `src/Takeover.h`,
`data/F4SE/Plugins/Rapport/takeover.json`.

**Why stop rather than uninstall.** On a live playthrough a stopped quest is strictly safer: the plugin
stays in the load order, every form it owns still resolves, and no script instance in the save is left
pointing at nothing.

**The principle: automatic, but never secret** (A-11). Every rule detects before acting, records the
previous state, is switchable individually, and names the change in the log with its reason. A mod that
silently changes another mod's settings is indistinguishable, from the outside, from a mod that breaks
it.

**Each entry names the feature that owns it**, so switching aftermath off restarts CumOverlays *without*
restarting Sex 'Em Up.

**What is deliberately left running.** `CMkz_SemenLib` — it is the library Rapport calls, and stopping
it would stop the thing we are trying to use. Only Moisturizer's event *handler* is stopped.

**The obligation this carries**, stated because it is the price of the decision: a stopped quest stays
stopped. Rapport must start both again the moment its aftermath feature is switched off, and the
uninstall instructions must say to switch it off before removing Rapport.

**Evidence.** Both directions, in the run logs:
`takeover: STOPPED CumOverlay_Main - ...` and
`takeover: 2 quest(s) from CumOverlays.esp will be started again - ...`.

The Autonomy Enhanced entry is declared although that mod is currently disabled on the reference
machine — an entry for a plugin that is not installed costs nothing, logs plainly, and arms itself if
it is ever enabled (A-16). That arming path is **BUILT, NOT VERIFIED IN GAME**.

### The debug hub — VERIFIED IN GAME

**What.** One profile in `debug.json` drives diagnostics across the whole stack: AAF's settings through
`ChangeSetting`, any MCM mod's through `MCM.SetModSetting*`, and the engine's
`bEnableLogging`/`bEnableTrace` in `Fallout4Custom.ini` — backed up once, applied next launch, and said
so rather than pretending otherwise.

**Why.** Debugging this mod means debugging three at once and each keeps its settings somewhere
different. **The value is not switching diagnostics on, it is switching them reliably back off** — a
stray `troubleshooting_level` put six modal pop-ups in front of the owner and nothing but memory would
have turned it off again.

`debug` is the development default and `off` is the shipping default; Rapport warns loudly at startup
whenever a non-`off` profile is active, because the only thing that stops a development default
reaching a release is somebody noticing (A-20).

**Evidence.** The warning in every run log:
`debug hub: profile "debug" is a DEVELOPMENT profile. Papyrus tracing is on and it slows the script engine.`

### The AAF watchdog — VERIFIED IN GAME

**What.** AAF's readiness is reported on every poll, immediately before `Pump` so the number acted on
can never be stale, together with `UI.IsMenuOpen("HUDMenu")` — the exact condition the cure needs, not a
proxy for it. When readiness has been wrong for `AAFReviveGraceSeconds`, Rapport calls AAF's **own**
`EveryTime_Initialization()` again: the updater quest first, then the main quest. Up to
`AAFReviveAttempts` times, `AAFReviveRetrySeconds` apart, and **never while a scene is running** — a
running scene is proof AAF was working when it started, and restarting its interface is exactly what
would end it. `src/AAFHealth.h`.

**Why.** See §8, finding 1. It is the single most valuable thing this project has measured.

**Why the HUD check is not optional.** Every restart AAF performs ends in
`UI.Load("HUDMenu", "root1", "AAF.swf", ...)`, and `UI.Load` puts an asset inside a menu that must
already be open. On a load screen it is not. Firing the cure into that emptiness does not fail — it is
counted as an attempt while having tried nothing.

**Evidence.** The cure is verified **3/3 in game**, recovering in about four seconds each time
(`docs/aaf-under-the-hood.md` §1).

`kRestartAAFQuest` — the harder `Stop()`/`Start()` reboot, carrying `GAME_DATA` across by hand exactly
as AAF's own updater does — is **BUILT, NOT VERIFIED IN GAME** as a successful cure. What *is* measured
is that a restart performed *before* blanking the stale path buys nothing: the quest came back running
and AAF stayed deaf. That is why the ordering is fixed.

`kAskStartAAF` asks the player before starting AAF's main quest, because the reason it is stopped may be
that AAF is being removed from this save — which nothing here can see and the player can.

---

## 8. Findings about AAF that any AAF mod author can use

These are why `docs/aaf-under-the-hood.md` exists. All 21 were measured against a running game or read
out of AAF's own decompiled sources; none is documented by AAF, and several contradict what its API
looks like it promises.

The five that change what somebody else would build:

1. **AAF goes permanently deaf after a load, and it is a race** (§1). Roughly half of all save loads.
   No error, no event, no indication anywhere. `getSWFPath()` lives in the **save**, and only the
   updater quest blanks it; whichever of two quests wins the `OnPlayerLoadGame` race decides whether
   AAF loads its interface or reboots a menu instance minted in a previous session that no longer
   exists. The same path came back byte-identical across four game launches —
   `root1.instance166.instance165` — where a real `UI.Load` mints a new instance number every time.
2. **`ChangePosition` never works here — 26 refusals out of 26** (§15). With tags, with a named position
   id, and with **no filters at all**. The last is what settles it: a call asking for nothing in
   particular is still refused, so this is not a content problem and no tag list fixes it.
3. **`StartScene` stamps the actors busy, and only a finished scene clears it** (`docs/aaf-api.md`). A
   request that never becomes a scene leaves an NPC unusable by every AAF mod **forever**, saved into
   the save. One NPC picked by three failed runs in a row stopped being usable at all. Check
   `HasKeyword(AAF_ActorBusy)` before asking, and clean up after every request that does not become a
   scene.
4. **A Papyrus stack does not come back from an AAF call** (`docs/two-lifetimes.md`). The call delivers;
   the stack does not continue, and Papyrus will not start a second `OnTimer` while the first is still
   running, so one stuck poll is every poll after it. `CallFunctionNoWait`, one AAF call per stack.
5. **`OnAnimationChange` is the stage-advance hook; `OnStageEvent` is silent** (§16). Registered and
   fired **zero** times across three complete scenes. `OnAnimationStart` fires once per scene — 2 Start
   events against 10 Change events — so anything forwarding only `Start` freezes on the tree's opening
   position for the whole rest of the scene.

The rest — `GetAAFStatus`'s three states, `<defaults>` inheritance inverting an XML census,
`includeTags` being an AND, `BSFixedString` case interning, `UI.Load` needing an open menu, the
declared-versus-actual tree time ratio (1.50× and 1.89× on two samples, so there is no calibration
factor), branch names promising an orgasm 27 times out of 61, and `startEquipmentSet` replacing AAF's
undressing — are in the same file.

---

## 9. Tried and removed

Kept because the measurement is the finding.

| | What it was | Why it went |
| --- | --- | --- |
| **`ChangePosition` staging** | A stage moved the running scene to its next mood. | Refused 26 times out of 26, including with no filters at all. `FindMatchingAnimations` agreed from the other side: 0 for all 17 tags tried, including `PenisToVagina`, which 2,320 installed positions carry, while the same query with no filter came back non-zero. |
| **Relocation** | Move the pair to better furniture mid-scene: stop, walk, start again. | Built and **proven end to end**, then deleted with `ChangePosition` — with the scene no longer restaging, there is nothing to relocate for. It is in the history if a mid-scene change is ever wanted. |
| **The slot-0 blush** | Three head-slot overlay templates for a facial flush. | Built, shipped, watched across several scenes: faces unchanged. F4EE does not apply a head overlay. Removed rather than left dormant. |
| **`startEquipmentSet`** | Take a visible-holstered-weapons rig off for a scene. | Naming a start set **replaces** AAF's automatic undressing: both actors stayed fully dressed through a complete sex scene. Nothing in `Rapport.log` or `Papyrus.0.log` mentioned equipment — visible only on screen, which is the worst shape of failure. `ApplyEquipmentSet` is **untested and left that way deliberately**: it is additive in principle, and "it also breaks undressing" is unknown, not established. |
| **`FindMatchingAnimations` as a pre-check** | Ask AAF whether a pair has content before spending a 90-second walk on them. | Rejected twice over. It is asynchronous, its argument layout has never been observed in this project, and a stage stalling on an answer is worse than one quietly dropped. The deeper reason: a tag index knows a tag exists *somewhere*, never whether an animation exists for **this pair, in this furniture, here**. AAF's own refusal is the only real evidence, which is why a stage offers alternatives one at a time. |
| **Editing AAF's XML** | Override UAP's hidden climax positions back into visibility. | Rejected: it fights another mod's deliberate design and changes behaviour for every AAF mod on the install, to recover something the tree route already provides. |

Two toolchain facts learned on the way out of the equipment work, recorded so nobody re-derives them:
`Actor.GetEquippedWeapon` returns a `Weapon` and Fallout 4 ships **no `Weapon.psc`**, so the held weapon
cannot be read from Papyrus at all; and the base game has **no `GetWornItem`**, only
`UnequipItemSlot(int)`, so a mod that clears a biped slot itself can never identify what to put back.

---

## 10. Not built yet

From `docs/roadmap.md`, in dependency order, and stated as absent rather than implied:

1. **Roles** — who gave and who received, from `GetActorData` and the position's own role list. The
   tag-plus-sex inference covers mixed pairs; same-sex pairs are an arithmetic compromise. This is the
   last thing standing between aftermath being right and merely working.
2. **Body morphs** — `ApplyMorphSet`, the third of AAF's three appearance levers and the only one
   Rapport does not touch. Needs no textures; unlike expressions it is not instant, so it wants its own
   persistence.
3. **The Attraction stat** (A-3). Worth stating why it is worth doing: AAF's schema supports
   `relationshipStat` with persistence, decay and optional ActorValue backing, and **zero
   `relationshipStat` definitions exist anywhere** — not in AAF, not in any content pack. Across 12,120
   compiled scripts (1,071 loose, 11,049 inside 239 BA2 archives), **no mod other than AAF itself calls
   `GetAttraction`, `GetActorData`, `ChangeStat` or `ChangeRelationshipStat`.** The layer is empty, so
   `GetAttraction` has nothing to return today.
4. **The F4SE message API** for addons. The Papyrus half exists; the native half does not.
5. **M3 hardening** — interruption handling, travel and privacy.
6. **Player Proposals** — its own repository, not started. Flagged early: it needs **dialogue records**,
   a far heavier ESP structure than the single quest record `tools/make_esp.py` writes by hand. That is
   the one place the real Creation Kit would genuinely help, and it is worth solving before the design
   depends on it.

---

## 11. The toolchain, because it is part of the mod

The Creation Kit was never installed on the reference machine. `tools/papyrus_setup.py` reconstructs
what the compiler needs from the game's own files:

- **The user-flags file**, read back out of Bethesda's shipped bytecode — `Form.pex`, `Quest.pex` and
  `ScriptObject.pex` each carry the same flag table independently, which is three agreeing sources
  rather than one. The compiler accepted the reconstruction on the first attempt, which is the only
  proof that matters.
- **The base sources.** 10,271 vanilla `.pex` (7,875 from `Fallout4 - Misc.ba2`, 2,396 from the six DLC
  archives) decompiled with Champollion 1.3.2; 10,022 succeeded, 381 came out empty and are quarantined
  because they make the compiler abort, leaving **9,641 usable**.
- **`tools/pex_natives.py`** writes the sources Champollion silently drops. A script that is nothing but
  native declarations has no bytecode, so it produces **no file, no error and no quarantine entry** —
  `Utility` is exactly that shape, which is how `Utility.Wait` became unavailable with nothing saying
  why. The tool refuses rather than guesses: if a function turns out to have instructions it stops, and
  if the parse does not consume the file to its last byte it writes nothing.
- **`tools/restore_custom_events.py`** writes back the `CustomEvent` declarations a decompiler cannot
  recover — 16 on `AAF_API`, 17 on `AAF_MainQuestScript` — read from the literal in every
  `SendCustomEvent` call.

**Three costs of reconstruction, each of which bit:**

1. **No default argument values survive.** Papyrus compiles a default into the *caller*, so the value is
   not in the callee's bytecode for any decompiler to recover. Every call into a vanilla function must
   pass all its arguments explicitly. That is a compile error, so nothing slips through unnoticed — but
   where the real default is unknown, passing the wrong value changes behaviour quietly.
2. **Custom event names are mangled at compile time**, and compiling against restored declarations does
   not mangle them. `RegisterForCustomEvent(api, "OnSceneInit")` emits `OnSceneInit` and never matches;
   the sender emits `aaf:aaf_api_OnSceneInit`. Everything looks right — the script builds, registers,
   and receives nothing, ever, with no error because `RegisterForCustomEvent` returns void. Register
   with the name the sender actually sends, read from its own string table.
3. **A namespaced script cannot be compiled by path.** The namespace comes from the import paths, not
   from the file. Also: `Native` is a reserved word, which is why the native declarations live in
   `Rapport:Core`.

**And one test lesson worth repeating.** `tools/make_overlays.py` generates the sweat textures and their
materials. A BGEM is parsed positionally, and a first attempt rebuilt the header field by field from a
reading of the format, producing 65 bytes where the working file has 63 — shifting every field after
them. It still passed a "round-trip test" that scanned the result for plausible strings, **which is a
test that cannot fail**. The honest test is to rebuild a material the engine already loads and compare
bytes, which is what the tool does now.
