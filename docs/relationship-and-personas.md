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

### R-4 measured (2026-09-21, the `rank <a> <b>` dev verb, Dugout Inn and Diamond City)

| Pair | Master's RELA record | `GetRelationshipRank` | Association |
| --- | --- | --- | --- |
| Clarence + Ann Codman | ClarenceAnn, Spouse | **4** both ways | Spouse |
| Vadim + Yefim | VadimYefim, Siblings | **3** both ways | Siblings |
| Cathy + John | CathyJohn, **ParentChild** | **3** both ways | ParentChild |
| Vadim or Yefim + Scarlett | VadimScarlett / YefimScarlett, BossEmployee | **1** both ways | BossEmployee |
| Cathy + Vadim | none | **0** | none |

It is Skyrim's scale: Lover 4, Ally 3, Friend 1, Acquaintance 0. The RELA record's own rank byte
is a different encoding (siblings store 1, the employer pair 3), so read the rank through the
function, never off the record. No negative pair was at hand; the negative half is assumed
symmetric until one is measured.

**`HasFamilyRelationship` is true for spouses as well as blood relatives** (the Codmans: family
True, blood False). So it cannot tell the couple from the siblings. The import reads the
association types (R-3) and keeps two flags:

- **incest**: Siblings, ParentChild, GrandparentGrandchild, AuntUncle, GrandAuntUncle, Cousins.
  In-laws are not blood.
- **partner**: Spouse or Courting.

Starting bond = `clamp(rank * 0.15, -0.6, 0.6)`, plus 0.20 for a partner (the Codmans start at
+0.80). Blood adds nothing either way.

An earlier note here called Cathy and John "married". The master says mother and son, and the
engine's answer was right.

## R-14 - Incest is a FLAG, never a refusal (owner, 2026-09-21)

Owner's words: *"we shouldn't block incest ... add isIncest flag and it will be used later for
my idea about NPCs reactively have attitude to each other and incest will be 'bad thing'. And in
a post-apocalyptic world incest would be really common 'bad thing'."*

So nothing in Rapport or Chemistry refuses a pair on blood. The store records it
(`Rapport:Core.IsIncestPair`, a co-save flag) for the attitude layer to come, where the people
who know treat it as a bad thing. A blood pair's bond is the engine's rank like anyone else's.
This settles the question that was open here: *should Chemistry refuse family pairs?* No.

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

## R-10 - What RAISES a relationship, and the double-count waiting to happen (2026-09-21)

Owner's roadmap: *"increasing relationship when pair pick each other with every new sex +
relationship affect decision making of parity with non linear curve"*. Both halves, and the
trap between them.

**The write path.** A completed scene raises that pair's value, with **diminishing returns** —
the first time two people sleep together means more than the fortieth, and without a curve the
number saturates and stops carrying information. Dialogue (the new mod), gifts and vanilla's
own `OnStoryRelationshipChange` raise it too, so the store's API is *"add this much, for this
reason"* rather than a sex counter. The reason is worth recording: it is what lets a later mod
ask *why* two people are close.

**THE DOUBLE-COUNT.** Chemistry `C-3` already gives **+0.15 per previous scene together, capped
at +0.45**, read from `PairSceneCount` in the co-save. If the relationship store also rises with
every scene and Chemistry then scores the relationship, **the same fact is counted twice** and
the cap that `C-3` exists to enforce stops being a cap.

So one of these, decided before either is built:

1. `C-3` becomes a **read of the store** — `PairSceneCount` is retired and the history bonus is
   just the relationship term. Cleanest, and it is what the store is for.
2. The store tracks relationship, `C-3` keeps tracking *scenes*, and they are scored as
   **separate things** on purpose — "we have history" and "we are close" being genuinely
   different claims.

Option 1 unless somebody argues for 2. Writing it down because this is exactly the kind of
quiet arithmetic collision that ships and is never noticed.

**Where the curve lives: Chemistry, not Rapport.** Same split as everything else — Rapport
stores the number, the consumer decides what it is worth. A non-linear response belongs with
the policy that has a bar of 0.90 to clear, not with the table that holds an integer. Two mods
could then weigh the same relationship differently, which is the point of the layering.

And per `R-4`: **the curve cannot be designed until the rank scale is measured.**

## R-11 - The player is a pair member like any other (2026-09-21)

The owner wants *"player-npc relationship powered interactions in new mod"*, so the store must
key player↔NPC pairs with the same machinery as NPC↔NPC. No second table, no special case.

The engine agrees: `GetRelationshipRank(Actor)` takes any actor and the player is one. Rapport
already resolves the player as a form id like anything else.

The **consequences** differ even though the storage does not — the player has no persona to
derive (R-7 is about NPCs reacting to *you*), and `R-5`'s "written on interaction" is doing more
work here, because the player interacts with far more NPCs than any NPC does.

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

## R-9 - Scene barks: the decision already knows what to say (2026-09-21)

Owner's idea, and it is the **best first use of the voice pipeline**. When a scene starts,
the actors say something that fits the decision that produced it:

| the decision | roughly |
| --- | --- |
| `quickie` chosen | *"we need to make this quick"* |
| three or more watching | *"I don't care that they're staring"* |
| their own home, `athome` | something unhurried |
| `tender`, high relationship | familiar, affectionate |
| first time for this pair | nervous |

**Every input already exists.** Scenario, observer count, whose cell it is, shared faction,
time of day, interior, and the pair's history are all computed before the scene starts and
are already in the log. The line selection is therefore **free** — no new measurement, no new
pass, no new cost. That is what makes this cheap rather than merely nice.

### It needs no dialogue system at all

This is the important finding, and it is why this ships before N-2 rather than after:

    // Sound.psc
    Int  Function Play(ObjectReference akSource) Native
    Bool Function PlayAndWait(ObjectReference akSource) Native

A `Sound` form points at an audio file **by path**. So a bark needs a Sound record in
Rapport's ESP and a generated file on disk, and nothing else:

- **no `TopicInfo`**, so no CK-authored dialogue tree
- **no form-id filenames** — the `<VoiceType>/<TopicInfoFormID>.fuz` convention in N-5 is a
  *dialogue* constraint and does not apply here
- **no `.lip`** — lip sync is optional, and during a scene the face is already being driven
  by Rapport's own mfg morphs. The mouth not moving costs nothing here, where in dialogue it
  would be glaring.

That last point is why this is the right first build: it exercises **generation → packaging
→ playback end to end** at small scale, and proves the pipeline without depending on the one
part of the plan that might fail.

### Shape

Same as every other config Rapport already has. `expressions.json` maps act and stage to a
face; this maps **situation to a line set**, and the sets carry variants. Selection per
actor uses the R-7 trick — derive from the form id — so the same NPC always speaks in the
same register, which is what made the six expression styles read as character rather than as
a shuffle.

`ActorBase.GetSex()` picks the voice at minimum. The Sound route is not bound to the NPC's
`VoiceType` at all, which removes N-5's multiplier here — a small set of voices covers
everyone, and the mismatch a dialogue mod cannot get away with is invisible in a bark.

### Two things to decide, neither blocking

- **Subtitles.** `Sound.Play` shows no text. `Debug.Notification` works (F4MCP's `say` verb
  proves it) and could carry the line as a HUD toast, but it is best-effort and is dropped
  during menus and loading. A bark nobody reads is still worth having; a bark with a toast is
  better. Not decided.
- **When it fires.** Prefer the moment Rapport already logs as `scene started`
  (`OnSceneInit`), not the walk-together window — that window is the one the AAF crash lives
  in, and while playing a sound does not itself add risk, there is no reason to put anything
  new inside it.

### Implementation anchor, scouted 2026-09-21

**The hook is `PapyrusLink::OnSceneStarted(std::int32_t a_request)`**, `src/PapyrusLink.cpp:1061`.
The bark goes immediately after `logger::info("request {}: scene started", a_request)` at
`:1082` — **below the stale-start guard**, so a late AAF event for a released request cannot
make somebody speak for a scene that is not theirs. That guard exists because AAF events have
been measured arriving 94 seconds late.

**What is in hand there** (`src/PapyrusLink.h:255-259`):

    _inFlightFirst · _inFlightSecond · _inFlightScenario · _inFlightDuration · _inFlightRequest

So **the scenario name is free** — `quickie` / `athome` / `tender` is already sitting there, and
that alone carries the best line in the owner's example ("we need to make this quick").

**What is NOT in hand, and is the first change to make:** `PairSignals`
(`src/Pairing.h:7-15` — `distance`, `sharedFaction`, `interior`, `night`, `observers`,
`playerNear`) is computed during pairing and **not carried to scene start**. Without it there is
no "I don't care they're staring", because the observer count is gone by then.

Snapshot it into the in-flight state at request time rather than re-measuring at scene start.
It costs nothing — the numbers are already computed — and it is the same discipline Chemistry
already applies to `WhosePlace`: *"From the snapshot, never recomputed."* Re-measuring would
also be **wrong**, not merely wasteful: AAF walks the pair for up to ~14 seconds first, so the
crowd at scene start is not the crowd the decision was made about.

Cell ownership is Chemistry's (`N-3`) and Rapport cannot see it, so a bark that wants *"not in
our own bed"* needs the addon to pass it with the request, or it is out of scope for v1.

### The audio pipeline, measured 2026-09-21

Everything here was read off real files, not assumed. The chain is
**ElevenLabs pcm_44100 -> WAV -> xWMA -> FUZ**, built by `scripts/pcm-to-fuz.py`.

**ElevenLabs.** Pro: 610,000 credits/month, 160 custom voice slots, 290 voice
add/edits. `pcm_44100` returns **headerless** 16-bit mono little-endian samples,
and the MCP server saves them with a **`.mp3` extension regardless** - the
extension is a lie, the bytes are PCM. Voice Design (`text_to_voice`) is the
route to many voices; Professional Voice Cloning is capped at **1** slot on Pro
and is the wrong tool anyway.

**What vanilla actually ships**, from `Fallout4 - Voices.ba2`
(`Announcer_AirportVoice/00112D18_1.fuz`) against our first render:

| | vanilla | ours |
| --- | --- | --- |
| magic / version | `FUZE` / 1 | same |
| payload | RIFF, tag `0x0161` (xWMA) | same |
| channels / rate | 1 / 44100 | same |
| chunks | `fmt`,`dpds`,`data` | same |
| lip data | 4,191 bytes | **0 - deliberate** |
| bitrate | **32 kbps** | 48 kbps |

The `dpds` chunk is the xWMA seek table and the engine needs it; `xwmaencode`
emits it for free. **Vanilla is only 32 kbps**, so the game's own format is the
dominant quality bottleneck - rendering from PCM still avoids a cascaded lossy
encode, but the gain is smaller than the source bitrate suggests. We sit at 48,
above vanilla, because there is no reason not to.

**`lip: 0` is correct here, not a shortcut.** `Config.h` blocks the face for the
length of a scene (`blockAnimationFaces{true}`) precisely because the engine's
facial idle "writes the same morphs to blink, breathe and **talk**", and two
writers on one morph is the flicker. Lip data would be a third writer fighting a
block Rapport installs itself. So `FaceFXWrapper` is not a missing dependency.

**`xwmaencode.exe`** lives at `D:\GOGGames\Fallout 4 GOTY\Tools\Audio\` -
the game's own copy. It has been seen to **exit non-zero while still printing a
plausible-looking message**, and on a locked output file it fails with
`ERROR_SHARING_VIOLATION`; check that the artifact exists and is non-empty
rather than trusting the exit code.

### R-9 delivery: TopicInfo records (owner poll, 2026-09-21)

Chosen over plain `SNDR` + `Sound.Play` **for real subtitles** - a half-whispered
line is easy to miss over combat or a radio. The cost is accepted: DIAL/INFO
records per line, a fixed audio path, and a heavier generator.

    Function Say(Topic akTopicToSay, Actor akActorToSpeakAs, Bool abSpeakInPlayersHead,
                 ObjectReference akTarget) Native

Four arguments, **no defaults** - decompiled base sources carry none, so every
call passes all four. `SayCustom(Keyword, ...)` exists too and may be the better
fit if we ever want the engine to pick among tagged topics.

**The file path, established from a MOD archive rather than vanilla** (vanilla is
always load order `00` and therefore proves nothing):

    Sound/Voice/<Plugin.esp>/<VoiceType>/<FormID & 0x00FFFFFF, 8 hex>_<n>.fuz

`LobotomitePack.esm` ships `PlayerVoiceFemale01/000022DA_1.fuz` - the load-order
byte is written as **`00`** and masked at lookup, so filenames survive any load
order. Casing varies within that archive (`0001dbd7` beside `00026A5A`), so the
lookup is case-insensitive. The folder name includes the extension.

An actor whose voice type we did not render falls back to **silence with a
subtitle** rather than a wrong voice - acceptable degradation, and it means
coverage can grow one voice type at a time.

**VERIFIED 2026-09-21, in the running game: `Say` works while AAF has the actor busy.**
Mayor McDonough, generic greeting topic `000048EB`, fired 4.3 seconds into a running AAF
scene - he spoke, voiced and subtitled: *"As much as I love talking to the people, I'm a busy
man."* Outside any scene the same topic gave *"I hope you enjoy your stay in Diamond City"*,
so the engine is doing real line selection under the topic rather than replaying one line.
The TopicInfo route stands.

Four things established getting there, each of which would have produced a wrong answer:

- **`IsInScene()` cannot see an AAF scene.** It reports the engine's own quest-scene
  system, which AAF does not use, and returned False for an actor mid-animation. Never
  use it as the "is this actor in a Rapport scene" test - ask Rapport.
- **Rapport runs one scene at a time and autonomy refills the slot within seconds.** A
  requested scene is REFUSED while another is in flight. `pause` first, then `request`,
  then `resume`. The third `request` argument is a SCENARIO name, not a duration.
- **The reconstructed base scripts have no `Topic.psc`, `TopicInfo.psc` or `VoiceType.psc`**,
  so any script naming those types fails to compile. fo4-mcp carries import-only stubs in
  `papyrus-stubs/` - never under a directory compiled with `-all`, where they would become
  .pex files shadowing the game's own types. Overture will need the same.
- **The screenshot was lying, twice.** At 125% display scaling a DPI-unaware capture took
  only the top-left 80% of the frame, dropping the bottom strip where subtitles and the
  loading spinner live - a spoken line read as silence, a healthy loading screen read as a
  hung game. And `CopyFromScreen` photographs whatever is ON the screen, so an overlapping
  window replaced four mid-scene frames. The owner caught the first by ear.

### R-9 wired (2026-09-21) - built, awaiting an in-game run

- **Choosing is C++, speaking is Papyrus.** `src/Barks.cpp` picks the line and
  queues `Order::kSayTopic` (25); the bridge calls `Say` on its own thread
  (rule 1). The handler sits ABOVE the `_api` check - Say is not an AAF call -
  and DROPS a line whose speaker is unloaded instead of deferring it.
- **The table is generated**: `scripts/build-barks-table.py` writes
  `data/F4SE/Plugins/Rapport/barks.json` from `make_dialogue.load()`, the same
  function that numbers the ESP. Topic ids are FILE-RELATIVE; the bridge uses
  `GetFormFromFile(id, "Rapport.esp")`. CI runs `formids.py --check` and
  `build-barks-table.py --check` before the build; the latter was proven to fail
  on a one-id drift.
- **Who speaks:** the first actor opens on `scene started`; the second answers
  `responderDelaySeconds` (4.5) later, rounded up to the next poll. The reply is
  cancelled on every scene-end path, and dropped as a backstop if its request is
  no longer the running one. No scenario -> no bark (the bank is per scenario).
  Assumption, not measured: the addon's FIRST actor is the initiator.
- **Sex** comes from the bridge's `NoteActorSex` (already reported at request
  time). Unknown sex gets only the ungendered lines. Coverage: all 48
  persona x scenario x role x sex cells have >= 6 lines.
- **Persona (R-7) is hashed before the modulo**, `(id * 2654435761) >> 16 % 4`.
  Measured on 25,000 simulated ids: a plain `id % 4` would have locked persona to
  the face style's parity - an odd-styled face could NEVER be mercantile or
  vulgar (0.0%). Hashed, every persona is 24.6-26.0% inside both. The persona list
  order is save-facing: append only (PERSONA_ORDER in the generator).
- The same NPC never repeats the previous line for the same persona/scenario/role.

**The ESP generator will need to emit Sound records.** `tools/make_esp.py` already emits
quests, so the machinery exists; SNDR is new work but small.

---

## R-12 - Observer reactions fire on APPROACH, not on scene start (2026-09-21)

Owner's idea and owner's refinement, and the refinement is the better half. The
first proposal was "a few seconds after the scene starts, pick a watcher". The
owner's version: **an NPC who comes close enough while a scene is already
running** gets a chance to react.

That is emergent rather than scripted. Someone wandering past reacts to what they
walked into; a timer firing a line at a pre-selected watcher is a cutscene. It
also costs nothing extra - Rapport already polls while a scene runs.

**The policy:**

- **One roll per actor per scene.** Keep a set of actors already evaluated for
  the running scene. On each poll, any uninvolved actor inside `observerRadius`
  who is not in that set is added and rolled exactly once. This handles both
  cases with one rule: somebody already standing there when the scene began is
  evaluated on the first poll, somebody who walks up is evaluated when they
  arrive. **Lingering must not grant repeated rolls** - "went close enough" is a
  transition, and rolling per poll would make a bystander who stops to watch
  eventually certain to speak.
- **Close enough AND able to see it** (owner, 2026-09-21: "when character is
  close enough and can see it"). Distance alone is wrong: a settler inside
  `observerRadius` on the other side of a wall, or facing away through a
  doorway, has nothing to react to. An actor becomes a candidate only on the
  poll where they are inside the radius AND have line of sight to one of the
  two participants (engine detection LOS, e.g. `HasDetectionLOS`). The roll
  happens on that first poll where both hold - so someone who walks round the
  corner and *then* sees it is rolled at the moment they see it, not when they
  first came within range behind the wall. Unseen-but-near actors are NOT added
  to the evaluated set; they may still get their one roll later.
- **Chance, not certainty.** Roughly one in three, in `scoring.json` so it is
  policy rather than code. The owner's word was *noticeable*, not constant.
- **A per-actor cooldown across scenes**, so one settler does not become the
  village commentator. Same shape as Chemistry's per-actor scene cooldown, and
  for the same reason.
- **`alone` or `crowd`** comes from whether any OTHER observer is present. A
  watcher on their own is furtive; one in a crowd plays to the room. The line
  banks are written to that split.
- **Persona is the speaker's own**, derived from their form id (R-7). The same
  event getting four different reactions is the persona system made visible, and
  it is the cheapest demonstration of it we will ever get.

**What has to change in the framework first.** `PairSignals::observers` is a
`std::uint32_t` - a COUNT (`src/Pairing.h:13`). Rapport currently knows how many
uninvolved actors could see the spot and **not one thing about who they are**. A
reaction needs the identities: the form ids, to derive persona, to pick a voice
type, and to keep the per-actor sets above.

So the count widens to a list, and `observers` stays as its size. This is the
same shape as the R-9 finding that `PairSignals` is not retained to scene start:
the framework measures the right thing and then throws away the part an addon
needs. Both are cheap to fix and neither is discoverable once the lines exist and
nothing plays.

**Not Chemistry's.** Any second mod that wants bystander reactions wants this
identically, which by R-1 puts it in the framework.

### R-12 as built and verified in game (2026-09-21)

`src/Watchers.cpp` (framework service) + `SweepWatchers` in `Bridge.psc`. Heard in game: Ivy saw a
scene and spoke; McDonough, behind a wall, heard it; Cathy's line queued behind another's and came
nine seconds later.

1. **Notice.** From 10s into a scene, each poll the bridge finds NPCs within 900 units
   (`FindAllReferencesWithKeyword`, ActorTypeNPC). The first time an adult who is alive and not
   fighting is found, their HEAD turns to the scene (`SetLookAt(a, False)`, no walking) - owner's
   design. Heads are released when no scene is running, which also covers a save loaded mid-scene.
2. **See or hear, on a LATER sweep.** Sees = `HasDirectLOS` from the watcher's `Head` node to a
   participant's `Pelvis` (else `Head`). Hears = within 600 units, walls or not (owner: someone next
   door hears it and would comment). A hearing-only watcher never gets one of the 16 lines that talk
   about seeing (tagged by `build-barks-table.py`).
3. **One roll per watcher per scene**, 33%; per-actor cooldown 300s; winners QUEUE and speak one
   per sweep, 6s apart - two reactions are both heard, in turn. Persona is the watcher's own (R-7);
   voice through Voices (V-25). Settings: the `observers` block of `barks.json`.

**Measured dead ends, so nobody re-tries them:**
- `HasDetectionLOS` is the STEALTH system: friendly townsfolk do not detect friendly NPCs, and it
  said no for a whole scene to two watchers whose heads were turned to it.
- `HasDirectLOS(x, "", "")` runs root to root, along the floor: the furniture the pair lies on
  blocked it on every sweep. Head->Pelvis found a gap past a wall on 3 of 5 sweeps.
- The first queue-less version dropped a second winner on the same sweep; queueing fixed it.

A watcher who noticed but never saw or heard is named in the log when the scene ends, so a silent
bystander in a test explains themselves.


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

**And the owner's point: five thousand may not frighten us.** With ElevenLabs over MCP and a
subagent fan-out, generating many variations of the same line across many voices is
*embarrassingly parallel* — each line-and-voice is bounded, independent, verifiable by
listening, and there are far more than three of them, which is exactly the shape that wants a
fan-out rather than a loop. The generation cost stops being the constraint.

What remains the constraint, and should be sized honestly instead: **authoring** (somebody
writes the lines and decides which situation each belongs to), **packaging** (`.fuz`, and
`.lip` if dialogue needs it), and **review** — a generated line that is subtly wrong for the
moment is worse than no line, and nothing but a person listening catches that. Plan the count
around what can be *reviewed*, not around what can be generated.

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

## N-7 - Companions get their own module, with their own relationship system (owner, 2026-09-21)

Owner's words: *"Sex with companions will be separate module in our next mod and companions will
have standalone unique relationship system; it will also bound to relationship db but will have
bunch unique modifiers."*

So: companions are NOT on Rapport's `ambientQuests` list, and Chemistry's autonomy never takes them
on its own. The companion module lives in Overture. It reads and writes the same store (`PairBond`,
`AddBondBetween`, the incest and partner flags) and layers companion-only modifiers on top: affinity,
the companion's own history with the player, whatever that design settles on. Nothing about it is
designed yet. This entry exists so the idea is not lost.

## Deliberately deferred

- **Factions.** The owner's own note: *"i dont have any cool ideas here besides standard
  boring - more respect with factions more relationship buff"*. Agreed, and it is already
  partly covered — shared faction is +0.6 in Rapport's scoring. Nothing more until there is
  a reason.
- **Attitudes and reactions** (incest, cheating, being badly treated). The owner explicitly
  wants this built as a stub with no consumer. Keep it that way: an unused system with one
  hook is cheap; an unused system with a policy layer is a maintenance cost.

## N-6 - The counting exercise, answered: Overture fits (2026-09-21)

N-2 asked the only question that decides whether this mod is real: **how many
authored lines does one persona-driven exchange actually need**, and is that
number fifty or five thousand. It is now measured rather than guessed.

**Stage one, written in full** (`scripts/build-overture-lines.py`,
`voice/overture-lines.json`): the first approach, four personas, four registers,
plus the place-recoil N-3 demands. **48 lines, 2,143 characters.**

The model it encodes: the player picks a **register** to approach with, the NPC
has a **persona** (R-7), and whether they match decides land or miss. The player
is never told the persona - reading it is the game.

| register | appeals to | it is |
| --- | --- | --- |
| `offer` | mercantile | caps, a gift, something material |
| `charm` | romantic | fancy words, patience, a compliment |
| `blunt` | vulgar | crude, direct, explicit |
| `linger` | reticent | say little, stay, simply be present |

**Place overrides both**, which is what makes N-3 load-bearing rather than
decorative: an intimate register in a public room **recoils even on the persona
it would otherwise land with**. The vulgar NPC likes being propositioned and
still does not want it shouted across a market.

**The extrapolation.** Assume a real exchange escalates over three stages and
wants three variants rather than two - about **9,644 characters per voice type**:

| coverage | characters | of a Pro month |
| --- | --- | --- |
| 6 core settler voices | 57,861 | **9%** |
| all 32 voice types | 308,592 | **51%** |

**So the answer to N-2 is: it fits.** Full coverage of every voice type costs
about half a month's allowance, and the cautious version costs under a tenth.
Generation was never going to be the constraint; this confirms it with a number
instead of a hope. **Authoring is the constraint** - somebody writes the lines
and decides which situation each belongs to - exactly as N-2 predicted.

**Two things this does NOT settle.**

- **The player's half.** FO4 player dialogue is voiced by the protagonist, and we
  cannot match that actor. Whether the player's side is silent, text-only, or
  scavenged from vanilla is open (N-5, open question 5) - and it does not block
  authoring the NPC side, which is why stage one exists without it.
- **`.lip` files.** V-5 established that `lip: 0` is correct for scene barks,
  because Rapport blocks the face for the length of a scene. **That reasoning does
  not carry to dialogue**: an actor in conversation is not in an AAF scene, nothing
  is blocking their face, and a talking head with a still mouth is conspicuous.
  Open question 6 stands, and it stands harder for Overture than it did for R-9.
  **Settled 2026-09-24:** every line ships lip sync, scene barks included (the owner; V-5 is superseded,
  see VOICE-GUIDELINES.md). Overture's lines already had it.

---

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
7. ~~**How many voice types the mod covers** (N-5).~~ **Sized by N-6**: 32 types exist and
   are rendered. Full coverage is ~51% of a Pro month, six core settler voices ~9%. The
   number is no longer unknown - it is a scope choice.
8. **Persona bias inputs.** Faction and class are available; whether they should bias the
   derivation at all is a taste question nobody has answered.

## Scope note

This roadmap is four mods: a relationship store, a persona system, a dialogue system, an
NPC-action API, and an attitude system. The **store is the keystone** — Chemistry consumes
it immediately, it is independently valuable, and every other piece reads it.

**Build order, revised once R-9 existed:**

1. **Scene barks (R-9)** — no dialogue system, no lip sync, no voice-type multiplier, and every
   input already computed. Proves generation → packaging → playback end to end at small scale,
   and is shippable on its own.
2. **Measure the rank scale (R-4)** — one harness session, and it blocks every curve.
3. **The store (R-1, R-2, R-3, R-5, R-6, R-10, R-11)** — the keystone. Chemistry consumes it
   immediately; resolve R-10's double-count against `C-3` before writing either.
4. **Personas (R-7, R-8)** — derived, so they cost nothing to add once the store exists.
5. **Dialogue prototype (N-2)** — the counting exercise. Still the piece that can invalidate
   the *new mod*, but no longer the piece that blocks everything else.
6. **The new mod (N-1..N-4)**, and the deferred attitude stub last.

R-9 displaced N-2 at the front for one reason: it de-risks the same pipeline while depending
on none of it.

---

## R-13 - Owner poll, 2026-09-21: relationships ship WITH the release

Four decisions, taken by poll before the publishing pipeline, and binding on the build:

1. **The release waits for the relationship module.** Barks, personas, bystanders and the voice
   fallback are verified, but they are published together with relationships, not ahead of them.
2. **The double count (R-10) is settled as option 1: Chemistry's C-3 becomes a READ of the store.**
   `PairSceneCount`'s history bonus is retired; a completed scene raises the relationship (with
   diminishing returns), and Chemistry's history term is the relationship value. One source of
   truth, so the cap C-3 exists to enforce keeps meaning something.
3. **Personas stay DERIVED (R-7 stands), plus hand overrides.** No save record for personas. A
   config keyed by form id gives named NPCs an authored persona; everyone else keeps the derived one.
4. **Personas drive behaviour, in two places:**
   - **Chemistry** - who pairs with whom and which scenario is chosen reads the persona.
   - **Overture** - player approaches read it (R-8: mercantile answers gifts, romantic words and
     patience, vulgar directness, reticent only history - which is what makes the store load-bearing).

## R-15 - How personas drive Chemistry, and Ivy's pin (owner polls, 2026-09-21)

- **Scenario + pairing.** The rules were approved as proposed and are Chemistry's DESIGN.md C-8:
  same persona +0.10; romantic with vulgar -0.10; in a crowd, +0.15 per vulgar member and -0.30
  per reticent member; mercantile adds nothing. In the open and uncrowded, a pair with a vulgar
  member gets `quickie` instead of `tender`.
- **Ivy is pinned vulgar** in `personas.json`, on her NPC record `IVYCOMP` (CompanionIvy.esm
  000803). The first pin named 000801, which is her voice type `_NPC_IVY`, and it was accepted
  without a word. The loader now refuses a pin that is not an actor or NPC.

## R-16 - Lovers by an addon's word (owner, 2026-09-23, through Overture O-15, O-27, O-28)

- **A lovers flag an addon sets** (`SetLovers` / `AreLovers` / `LoverOf` / `LoverCount` / `LoverAt`,
  pair flag `0x10`), kept APART from the engine's spouse and courting: `HasPartner` and `ArePartners`
  answer exactly what they answered before, and a consumer decides for itself whether a lover counts
  as spoken for. Chemistry counts the PLAYER's lover, behind its own MCM switch (its C-4 and C-9 as
  amended by O-15).
- **When:** Overture declares the player and an NPC lovers at a bond of 0.75 AND at least one scene
  together (O-27) -- never at the yes: a yes is a promise, the scene is the fact.
- **The end:** falling out (bond at or below -0.25) ends it (O-28), wherever the bond goes there --
  `AddBond`, and the vanilla seed if it lands on a declared pair -- and `SetLovers` refuses a pair
  already there. A death forgets it with the rest (R-6).
- **Kept:** the flag is a fact, so pruning never drops a lovers row, and the pair ceiling cuts them
  last.

## R-17 - The player's own request comes first (owner, 2026-09-23, through Overture O-16)

- **One scene slot, held for the player:** `ReservePlayerScene(akWith, seconds)` (at most 120 s)
  refuses every other pair's request until the held pair's own request is accepted, which lets it go.
  The refusal sits in the request funnel, so it covers addons AND Rapport's own autonomy.
  `PlayerHoldsSlot()` lets an autonomy mod skip its pass.
- **One hold at a time:** a hold for someone else replaces it, and the log says so. A release names its
  hold. Not saved: a load forgets it.
- **The pause holds off autonomy only.** A pair with the player in it is the player's own request, and
  the pause exists to clear the way for exactly that.
- **The door re-checks what the scan checked** (microscope pass 2): a request is refused if either
  actor is dead, not loaded, in an ambient conversation, or talking to the player -- the player's own
  request excepted from the last, since that is where its yes came from.

## R-18 - Scenes with the player (R-11's consequences, 2026-09-23)

- **The player never speaks a bark** (R-11: the player has no persona). The NPC still says theirs,
  and a scene the player asked for is narrated in the NPC's voice.
- **A yes never vanishes without a word:** a player scene that fails after it was accepted, or is
  given up before it ever started, is narrated ("... it didn't happen after all.").
- **Addons hear when it is over:** `Rapport:Bridge` sends `OnPlayerSceneRecorded` once a scene with
  the player has ended and been recorded (Overture declares lovers at that moment, O-27).
- **The player's history never ages out:** their actor record is exempt from pruning and the ceiling,
  so their scene count only goes up (Overture's jealousy counts from it, O-29).

## R-19 - Names for the nameless (owner, 2026-09-23, through Overture O-10)

"Generate persistent names for NPC if they didn't have names before in moment player approach them."
`Introduce(actor)` gives a generic NPC a first name and a surname, derived from the form id, so the same
person always gets the same name. Generic means a LABEL: a name on five or more NPC records, not all of
them Unique; a Unique NPC counts only when the name comes from their template. Never the player, anyone
who has ever been a companion, the dead, or someone with a custom name already. The game keeps the name
on the actor through saves (measured); the co-save keeps who was introduced and as which base, and a
lost name is given back quietly. `names.json`'s `keep` and `label` lists override the count. Settled by
measurement: the Unique flag alone left the Third Rail's "Drifter"s nameless, and a three-record rule
would have renamed Magnolia.

## R-20 - The Narrator says "you" (owner, 2026-09-23, through Overture O-34)

Every Narrator line about the player's own moment says "you" -- "You and Dottie are inseparable now",
"A first time for you and Dottie" -- never the player's name in the third person. The player goes
first in a pair; an addon's `{first}` is "you" too.

## R-21 - The base game's companions are pinned (owner, 2026-09-24, through Overture O-26 and O-39)

R-7 derives a persona from the form id, which knows nothing about who a companion is. The owner approved
Overture's table, and `personas.json` pins it:

| persona | companions |
| --- | --- |
| romantic | Piper, Preston Garvey, Curie |
| vulgar | Cait, Hancock (and Ivy, R-15) |
| reticent | Paladin Danse, Deacon, X6-88, Old Longfellow |
| mercantile | MacCready, Gage |

**How the ids were found.** Every id is the NPC_ record that carries CompanionActorScript. They were found
by scanning Fallout4.esm, DLCCoast.esm and DLCNukaWorld.esm for that script, not recalled. The scan finds
14 such records: these 11, plus Codsworth, Nick Valentine and Strong, whom Overture's approach never opens
for. Dogmeat carries no such script, and Automatron's Ada is in a file that was not scanned. Curie's placed actor (CurieRef 102249) is pinned beside her base NPC, because
she changes body mid-story and the reference is what stays.

**Why the pins matter.** They choose Chemistry's weights (R-15) and every Overture line these companions
say, including the vulgar persona's, which O-40 made explicit.

## R-22 - Every AAF scene gets Rapport's treatment, not just Rapport's own (owner, 2026-09-24)

**The gap.** The owner started scenes from AAF's own menu and saw stony faces. Every scene feature ran
for Rapport's own request and nothing else:
- `PapyrusLink::OnSceneStarted` and `OnSceneEnded` return early for any scene that is not the one in
  flight.
- Meanwhile the takeover (A-11) stops CumOverlays' and Commonwealth Moisturizer's AAF listeners for EVERY
  scene.

So an AAF-menu scene, or any other mod's, got no face, no Rapport voice and NO CUM AT ALL while Rapport was
installed.

**The owner's answers:**
- Asked through the anatomy session: "Yes, all AAF scenes" for faces.
- On the cum: "its ok we disabling default cum moiz listener bc we need have our ruling all the time just
  also do it on ALL aaf scenes". The takeover stays, and Rapport's aftermath covers every scene.
- Poll, 2026-09-24:

| question | answer |
| --- | --- |
| do other AAF scenes count in the relationship records (bond, "a scene together" for lovers, jealousy, a companion's wanting, the Narrator's milestones)? | **only the player's own, started from the AAF menu** (recommended). Other mods' scenes -- autonomy, harassment, anything non-consensual -- change no relationship |
| do Rapport's voices play on other AAF scenes? | **both** (recommended): the pair's barks and the bystanders' reactions |

**What identifies a scene, per AAF's own API documentation** (the moddingham wiki; read, never copied
into this repo):
- Every event carries the scene's actors at `[1]` (an `Actor[]` packed in a Var) and its LOCATION form id:
  `[3]` on OnSceneInit, `[5]` on the animation, stage and end events. The location is the ground actor or
  the furniture the scene is placed on.
- The bridge's "scene id" has always been that location id. It is unique per concurrent scene, so it
  stays the key.
- `meta` is whatever string the starting mod set. Rapport sets `Rapport,autonomy`, which makes "ours" a
  test of what the scene says, and not only of who is in it.
- `isNPCControlled` is false when the player drives the scene.
- A menu scene is read as: the player is in it, no mod's meta, and not NPC-controlled. **ASSUMED until
  measured**: the bridge logs every event's arguments, so the first AAF-menu scene settles it.

**Build order:**
1. Faces and aftermath.
2. The voices.
3. The records, after the menu scene has been measured.

**Forced scenes get the same treatment (owner poll, 2026-09-24).** Other mods' scenes can be non-consensual
(harassment, Violate-style), and DESIGN.md lists non-consensual content as a non-goal of Rapport's own
autonomy. Asked whether those scenes should get Rapport's pleasure faces, no face, or a separate distressed
face, the owner answered: **"We apply pleasure also for them turning it into bdsm and not rape so we even
fix it!"**
- Every AAF scene, forced ones included, gets the same pleasure-to-climax faces.
- The voices of stage 2 follow the same rule.
- The reading is the owner's: Rapport's pleasure reframes such a scene as consensual BDSM play.
- DESIGN.md's non-goal still stands for what Rapport STARTS. Its scenarios keep excluding Rough, Aggressive
  and Dom9, and only what other mods start is re-faced.

**Stage A as built (src/ForeignScenes.*, three microscope waves).** These are engineering readings, not owner
decisions. The numbers marked ASSUMED are for tuning.
- **Which scenes.** The bridge forwards any scene no request of ours claimed, except the dev verb's
  `Rapport,control`. It is "ours" (left alone) only when it carries `Rapport,autonomy` AND holds the pair in
  flight. A scene of ours whose request was already given up is treated like anybody's.
- **Who.**
  - Children are ignored completely (DESIGN hard rule; the owner restated it on 2026-09-24: "no any
    interactions with them", and "they will not pay attention to sex like adults do when being observers").
    - A child BYSTANDER changes nothing. ActorScan drops children before it counts observers, so they never
      make a place less private. Watchers never pick one, and a child's voice is always silent.
    - A child among a scene's own ACTORS, in AAF's actor list, means Rapport does not touch that scene at
      all. It gives no face or cum to anyone, and anything it put on the adults before the child was read
      comes off.
  - A race Rapport does not dress (races.json): no face and no cum, but it stays in the scene. Who an act
    landed on is decided with everybody there. A man with a female creature is a pair, so the cum is hers,
    and she gets none.
  - One actor is in one AAF scene at a time. A member who turns up in another scene, or in Rapport's own,
    finishes every other record holding them.
- **The face.**
  - Nothing before the first animation. AAF's walk happens BEFORE its OnSceneInit (OnWalkInit comes about
    10 s earlier), and the first animation follows the init within 0.10-0.33 s (measured,
    docs/runs/2026-09-18-first-complete-chain.log). A face at the init would be overwritten before it showed.
  - From the first animation, the act's family at a level from expressions.json's schedule, measured by:
    - tree steps for a tree;
    - AAF's own duration for a timed scene;
    - 150 s for a loop (ASSUMED).
  - The climax comes from a climax tag only, and after it the level never falls.
  - After the end, the afterglow, cleared after the same seconds as Rapport's own.
  - Skin heat is per actor and only climbs.
- **The cum.** It is decided once, from the last act, when the scene ends, and only if the scene ever
  animated: a walk given up on leaves nothing.
- **Ends that go missing.**
  - A timed, NPC-controlled scene with no tree that is well past its length (3x, or +300 s) is taken for
    over. Its cum is decided then, and the rest of it is left alone. A player-controlled scene never is: the
    player can hold their own scene open.
  - A scene not animating 180 s after its start is taken for never begun (ASSUMED, and very long, given
    the 0.1-0.3 s above).
  - A finished scene is remembered for 120 s with who was in it, including Rapport's own. That memory survives
    a new scene taking the place: back-to-back scenes on the same victim share a ground location, and the old
    one's late or duplicate end can come after the new one began.
  - An end or animation is told apart from the new scene's by WHO it names. The actors are read from AAF's
    own array, whereas what an end's position means has never been measured for a tree, or after the player
    changes position. An end naming someone the running scene lacks, and only people a finished scene had,
    is that finished scene's. With the same cast it is always the running scene's: swallowing a running
    scene's real end would leave its face on and lose its cum, which is worse than a rare second layer.
  - Someone moving straight into the next scene keeps their face and their sweat until that scene dresses
    them ("a body can walk out of one scene still sweating into the next"). A next scene that names no act
    takes their face off.
- **Known residual:** the bridge claims an animation event for Rapport's own scene by location alone. The
  late animation of an earlier scene whose ground location was one of our actors would be taken for ours
  and feed one stale set of tags. It would have to arrive after our 10 s walk, and the next animation
  overwrites it, so it is left alone. An actor check would not catch it anyway: both scenes share the actor
  whose id is the location.
- **Not handled:** AAF joins and exits, where an actor joins a running scene or leaves it (events carry
  exitingActors at `[6]` and joinedActor at `[8]`). Members only grow, and nobody has measured whether a join's
  OnSceneInit arrives at the parent scene's location. If it does, it would look like a new scene. Mods rarely
  use joins; Violate-style chains start separate scenes.
- **Open, for the owner:**
  - A same-sex pair still gets the cum on both (A-21's "both"). With a creature it reduces to the human.
  - The slot order that would settle both is readable now, but unmeasured for a same-sex pair.
  - Whether Rapport's MFG block fights other mods' AAF_BlockMFG keywords is unmeasured.

## R-23 - Debug triggers: scenes on demand (owner poll, 2026-09-24)

fo4-mcp cannot start AAF scenes, so the owner starts them himself. He asked for MCM buttons, then chose in a
poll:
- **Triggers:** all four.
  - Rapport, "a scene with the one I face": the player plus the NPC they face.
  - Rapport, "the one I face + their best partner".
  - Chemistry, "decide now".
  - Overture, "approach now" and "straight to a yes".
- **Gates:** every trigger twice.
  - FORCED skips the soft gates: the score bar, cooldowns, privacy, time of day and bond.
  - REAL obeys every gate the real path obeys, and says on the HUD which one refused.
  - The hard rules hold for both: adults only, alive, loaded, not fighting, a race Rapport dresses, and one
    scene at a time.
- **Input:** a Debug page in each mod's MCM, and MCM hotkeys (keybinds.json).

As built:
- **"In front of you"** is Rapport's `ActorInFront`, one definition for all three mods. It uses the player's
  yaw, with the same arithmetic as F4MCP's `aim` verb, which is measured in game.
- **Rapport REAL** runs one scan through the stand-in's own filters and ranks the pair with `RankPairs`. The
  player is never scored: a scene with the player is the player's choice.
- **Chemistry REAL** is its ordinary `Consider()` pass. FORCED takes the best pair Rapport offers.
- **Overture REAL approach** makes the player activate the NPC, so the engine picks the greeting as in play.
  **Straight to a yes** builds a finished stage-3 accept and hands it to `Finish()`, so it gets the same
  markers, Narrator line and scene request as a real one.

## R-24 - Rapport rules the faces it holds (owner, 2026-09-24)

The owner: "we need grab whole power on ruling things we rule in rapport including expressions bc we need to
be SOT".

**The measurement.**
- In a cowgirl the face looked like the blowjob: the jaw wide open, with a close-open flicker.
- `Rapport_Pleasure_3` had Jaw Open 85, unlocked.
- The engine builds a face as clamp(max(override, animation)) (0x6689D0; fo4-anatomy's field notes). So no
  AAF mfg set can close what the animation opens, and `lock` never helped.
- The engine also ignores MFG overrides on the upper eyelids: they are min(1, blink + animation). So
  Rapport's eyelid values never showed at all (the anatomy session read it from the executable).

**The fix, agreed with the anatomy session.**
- Anatomy's cbp.dll ("OCBPC plugin") owns the one hook after the merge (0x6860FA). It REPLACES the merged
  value with Rapport's for every morph Rapport owns.
- **Protocol:** F4SE messages from "Rapport" to "OCBPC plugin".
  - 'RFAS' sets a face: `{u32 version=1; u32 formID; u64 owned; float value[54]}`.
  - 'RFAC' releases one actor, or everyone with formID 0.
  - Their 'RFAH' hello at PostPostLoad turns it on. With no hello, Rapport's AAF path is the whole story, as
    before.
- **Own-all:** every morph 0-49 of a held face is Rapport's, and a morph the set doesn't name is 0. The
  exceptions:
  - the blink stays max(engine, ours), so held faces still blink;
  - Anatomy's contact mouth opens the jaw to fit while something is in the mouth, starting from our jaw;
  - while a held actor speaks one of Rapport's lines, the mouth morphs (make_mfg.py MOUTH: 29 at first, 23 since the measured list) are left to lip
    sync for 9 s. ASSUMED: the C++ side does not know a line's length. Superseded the same day when the hello
    carries feature bit 1 (anatomy f39831b, features 7): their side then gives the mouth ids to ANY line the
    engine plays on a held face, for its real length, and Rapport stops clearing mouth bits itself.
  - Also from that build: bit 2 lets the busy-mouth reaction RAISE brows, cheeks and nose above a held face
    during oral contact (the owner's A-26 poll: layered on top, raise-only).
  - A load now also sends a clear for everyone (formID 0), on top of their own release at every load.
- **Depth-driven brows (owner, 2026-09-24: "when penis go deep in throat broves sliding closer like
  хмурится").** Rapport authors both ends of the face; Anatomy supplies the depth, frame by frame.
  - 'RFAD' has the RFAS layout: `{u32 version=1; u32 formID; u64 blend; float value[54]}`. It carries the held
    face at FULL depth.
  - It is sent right after every RFAS for a set that has a deep face, and only when the hello carries feature
    bit 3. An RFAS with no RFAD after it means "no deep face now".
  - Their side computes value = lerp(held, deep, depth) for the ids in `blend`. On those ids their A-26
    raise-only terms stand down, so the frown is not lifted back up.
  - The deep face lives in make_mfg.py `DEEP` and goes to faces.json `"deep"`. It exists for Rapport_Oral only.
    From the pleading face it goes to: Brow Squeeze 85, Middle Brow Down 55, Middle Brow Up 0, Outer Brow Up 0,
    Outer Brow Down 30, Upper Eye Lid Down 75, Lower Eye Lid Up 45, Nose Up 30, Cheek Up 45.
  - The blend holds no mouth id: the mouth stays with the contact mouth and lip sync.
  - Anatomy agreed (fork 10ca01f, cbp.dll 6a40a1e7a848, their A-29). On their side:
    - w = inside × clamp(depth/6), the same depth signal A-26 reads;
    - the blend runs after our held face and before the contact mouth;
    - the blink still wins on the upper lids (18/41);
    - MOUTH ids are never blended, even if a mask names one;
    - an RFAD for a form with no held face is dropped and logged "not held".
  - STATUS: VERIFIED in game on 2026-09-25, with Rapport.dll 5635bbd1 and cbp.dll a0c8a574e403. The owner:
    "facial deep expressions in browes is very immersive".
- **The jaw is set deliberately now:** Anticipation 15, Pleasure 15/25/35, Climax 45, Oral 35 (the contact
  mouth's base), Kiss 15, Dazed 20.
- **Eyes:** the sets' eyelid values apply for the first time, as a floor under the blink. The pleasure and oral
  eyes will look more closed than before; worth a look in game.
- **Where:** `src/FaceAuthority.*` reads `faces.json`, which `tools/make_mfg.py` writes from the same table as
  the AAF XML. It is fed from `PapyrusLink::QueueOrder`, the one funnel every face and line passes.

## R-25 - No hugs where sex was asked for, unless someone is shy (owner, 2026-09-24)

The owner, testing R-23: "2 times at row they do hugging instead of real sex". Then: "if it was randomly picked
non sex i am ok with it. we can allow nonsex for only specific personas that could be too shy to sex sometimes
despite post apocalyptic morale".

**What happened (Rapport.log, 16:53-16:56).** Chemistry's Decide now chose `quickie`. Quickie has no tree stage,
so the bridge started the scene with no position and AAF picked from everything the pair could play. It picked
"(CHAK) Table Kiss A1", tagged `NonSex, Kissing, Foreplay`.

**The rule.**
- A scene that starts with no tree gets AAF's own default exclusions plus the non-sex markers, `NonSex` and `SFW`.
  Quickie starts this way, and so does every f_f pair on this install.
- If either member's persona is **reticent** (R-8: "nothing, the first few times"), the markers are not
  excluded, and AAF may pick a hug now and then. The player has no persona (R-11), so only an NPC makes that
  exception.
- A tree start is untouched: the tree already names its position.
- Settings: `scenarios.json` `"nonSex": {"tags": "NonSex,SFW", "allowedFor": ["reticent"]}`.

**Why these tags.** They are read from the installed packs (tags come from `positionData` and from the
`*_tagData.xml` files, which ADD to them; aaf-sot §9).
- `NonSex` is CHAK's marker: 39 installed positions of hugs, cuddles, kisses, snoozes and a slow dance.
- `SFW` is UAP's, on 11: Atomic Lust's embrace, kiss, cuddle and holding hands.
- Neither marker is on any position with a sex-act tag.
- `Kissing` was rejected, because at least 16 sex positions carry it too.
- About ten kiss-only positions carry neither marker (BP70's Kissing, Make Out, Smooching, Rufgt's Gay
  Kissing, ...) and can still come up by chance, which the owner accepted.

**Why the defaults are read from disk.** Setting `SceneSettings.excludeTags` REPLACES AAF's
`default_excludetags`, it does not add to it (AAF's SceneSettings docs). The factory holds a sentinel that AAF
resolves on its own side (aaf-under-the-hood §11). So Rapport reads every `Data/AAF/*_settings.ini` and takes
the value from the highest `priority`, as AAF merges them: `pose,utility` from AAF_settings.ini on this install.
It cannot see a value the player changed in AAF's own MCM, which AAF stores per save.

**Quickie steers (owner poll, same day).** Quickie's stage `include` (Handjob, Blowjob, PenisToMouth,
PenisToVagina, FromBehind) and `exclude` (beds, couches) used to reach AAF in no case, because only a tree stage
chose anything. The owner chose "steer + safety net":
- A tree-less scenario's first playable stage is its style. Its `exclude` always goes to AAF, shy pair or not:
  the shy rule changes what they do, not where.
- Its `include` goes to AAF as `includeTags` (ANY of them), but only when the pair has a man. Measured with beds
  and the markers out: F+M keeps 131 of 185 positions with the list, M+M 29 of 57.
- A SHY pair (a reticent member) gets no act list either. `includeTags` is an allow-list: asking for the acts
  would take away the hug or the kiss that the shy rule exists to leave possible (the sonnet review found the
  two rules colliding). Shyness changes what, not where: the beds stay out.
- Two women never get the list. 8 of their 24 positions carry one of those tags (2 on the ground), so asking
  would squeeze F+F down to those.
- **Any pose, the owner's standing rule:** "If for some reason we didn't find sufficient pose we fallback to any
  pose / We was doing such always for everything".
  - When AAF answers `[034] ... no animations` for a start that carried any of Rapport's filters (quickie's
    style, its act list, the non-sex markers), that pair's next start asks for nothing. AAF's own defaults alone
    apply, until a scene of theirs actually starts.
  - So a scene never fails twice over our filters. A hug is possible on that one retry; the owner accepted a
    random non-sex pick from the start.
  - Only `[034]` counts. An actor already busy, or AAF not ready, says nothing about the filters.

**The owner's rule for where this applies:** "our scenarios is for gameplay" and expressions are "related to act
MECHANIC". So:
- MECHANICS (faces, expressions, cum, aftermath) run on every AAF scene, the AAF menu included (R-22).
- GAMEPLAY (scenarios, position choice, the no-hug rule, quickie's style) applies only to scenes Rapport's mods
  start. An AAF-menu scene is mostly testing and stays unrestricted.
- All of R-25 lives in the bridge's own StartScene, which a menu scene never passes through.

**UAP originals (owner, same day): "let it as it is".**
- On 09-23, three positions from packs' ORIGINAL XML stood in idles, while their "[UAP] ..." copies animated.
  The cause is unknown (aaf-uap-original-positions-dead).
- The owner chose no machinery for it:
  - AAF keeps picking by itself;
  - a position reported broken is dealt with for that exact position.
- Where Rapport names a position itself (its tree choice), it takes the exact UAP twin: an original that runs the
  same tree as a "[UAP]" position gives way to it. On this install that is 2 trees, Atomic Spanking and Atomic
  Lust's love tree.
- The 9 original BP70 trees have no twin and stay.

**Debug triggers, same day (R-23 follow-up).**
- **A pair of one kind:** F+F, F+M and M+M, FORCED and REAL, as buttons and hotkeys. Each picks the best-ranked
  pair of that kind near the player. The NPC faced is one of the two when their sex fits; otherwise the HUD
  says they were passed over.
- **Refusals in words:** the scene funnel's door now says why on the HUD ("Rapport refused Elmer Pike +
  Drifter: Drifter (33008AF6) is in the middle of a conversation"). It used to say "the reason is in
  Rapport.log".
- **Chemistry:** its Decide now reads `Core.LastRefusal()` straight after a refused request and shows it.

## R-26 - Scenes inside tables: stage 1, measure before moving anything (owner, 2026-09-24)

The owner: "are we able to rule fundamental problem of aaf scenes - that scenes starting in inapropriate
places for ex INSIDE texture like table/clutter". AAF plays a ground position where the first actor
stands and knows nothing of the room. The lever is `SceneSettings.locationObject`: AAF's docs say a
non-furniture object passed there is used for its coordinates. Stage 2 will choose a clear spot and
hand AAF a marker there, for OUR scenes only. The AAF menu starts its own scenes and stays out of reach.

**Stage 1, built: detection only** (`src/Placement.*`).
- **When:** at each scene's first animation, ours (`NoteScenePosition`) and menu scenes
  (`ForeignSceneAnimation`), once per pair per 120 s.
- **What counts:** every loaded reference in the actors' cells whose base is solid (STAT, SCOL, MSTT,
  FURN, CONT, ACTI, DOOR, TERM, FLOR). Its bounds box is placed by position, Z rotation and scale, and
  checked against a cylinder around the actors, 20 to 110 units above their feet.
- **The cylinder's radius, ASSUMED:** 110 for lying and all-fours positions, 70 for standing ones, 90
  otherwise, and never less than the actors' spread + 40.
- **What is logged:** one Rapport.log line per scene, "placement: ... N in the way - <type> <id>
  (<plugin>) ref <id>, <depth>u in". Nothing in the world is touched.

**What the owner compares it with:** the scenes he sees clip.
- A clip with nothing logged means the test is blind somewhere: a neighbouring exterior cell, a tilt,
  or a type not listed.
- A log full of things that never clip means the radius or the band is too wide.
- Room shells (a box more than 4 radii wide that surrounds the spot) are counted apart, not listed.

**Stage 2, built the same day (the owner: "maybe u do and next stage too? and dont forget add good logging").**
Our scenes only. Just before StartScene, the bridge asks `Core.SceneSpot(slot0, slot1, position)`:
- **Furniture:** a position whose tree needs furniture is left to AAF, which places it on the furniture.
- **Already clear:** if AAF's own spot (slot 0's feet) is clear for the footprint, nothing moves.
- **Otherwise it searches.** Rings every 30u out to 330u, 16 points a ring; the first spot that passes
  all three tests wins:
  - **on the floor:** the footprint lies on the cell's NAVMESH, meaning its centre and 16 ring points at
    r and r/2 are all within 24u of level. The navmesh is walkable floor, baked around statics and
    furniture;
  - **nothing in the way:** no solid object's bounds box is in the footprint. That covers settlement
    builds and moved clutter, which the baked navmesh does not know;
  - **reachable:** a straight walk from slot 0 stays on the navmesh, 30u steps, no step over 36u.
- **The move:** one persistent XMarkerHeading (Fallout4.esm 00000034) is moved to the spot, faced like
  slot 0, and handed to AAF as `locationObject`.
- **The navmesh read:** CommonLibF4 only forward-declares the cell's NavMeshArray. It is read as a
  BSTArray of NavMesh pointers, each checked against the game's NavMesh vtable, inside a structured-
  exception guard. An unreadable navmesh means no move, logged.
- **Every decision is a "placement: request N: ..." line.** The first animation's survey then logs how
  far the scene's centre landed from our spot: that line is the measurement of whether AAF honours
  `locationObject` for a ground position (UNVERIFIED until the owner plays one).

**Stage 3, the first night's log (2026-09-25, four requests of ours).**
- **AAF honours the spot.** Requests 2 and 3 were moved 60u and 150u. Both scenes played 0u from our
  spot, with nothing in the way. VERIFIED.
- **Two were left to AAF.** Requests 1 and 4 were in small rooms. For request 1, only 5 of 176 candidates
  even had enough floor, and every one of those had something in it.
- **The owner watched both left-alone scenes and said they "looked fine".** One was a spooning pair with
  containers 38u into its r 110; the other a standing 69 with a container 39u in. The footprint is a
  circle, but a lying pair is a line, so an object that far in stands beside them.
  - **So a hit counts only when it reaches the inner 60% of the radius** (depth > 0.4 r, at least 8u; 44u
    at r 110). The moves that night were for 46-110u, and they still count.
- **The owner's call on cramped rooms: the LEAST-BAD spot.**
  - When no spot is clear, take the one that is on the floor, level and reachable and has the shallowest
    deepest intrusion. Use it only if it beats AAF's own spot by 20u.
  - The pose never changes for this. Changing it was the rejected alternative, because it is gameplay.
  - The log says "moved ...u to the LEAST-BAD one", or why the least-bad spot was not worth the move.
- **Log fix:** the list after "N in the way" now names only what N counts, plus "N brushing it".

## R-27 - Sexual orientation (owner poll, 2026-09-25)

The owner: "Besides persona characters also should have SEXUAL ORIENTATION. We need figure out this
mechanic."

**Settled by poll:**
- **Mix:** straight 70%, bi 20%, gay 10% among ordinary NPCs.
- **Hard line:** a straight man never agrees to sex with a man, whatever the bond. When asked, the NPC says
  so in character, so the player learns who is into whom. There is no "win them over": the rejected
  alternative was a strong bond that bends it.
- **Romanceable companions are playersexual,** as in vanilla: open to the player whatever the player's
  sex. Toward NPCs they have a real orientation. Ivy stays as her author wrote her.

**The shape (to build):**
- **Derived, like personas (R-7).** Orientation comes from the form id, so it is the same on every machine,
  costs nothing in the save, and needs no cleanup. It uses a different mix of the id than the persona does,
  so the two are not correlated. Named characters are pinned in `personas.json`, like persona pins (R-21).
- **Rapport owns it; the consumers read it.**
  - Chemistry pairs NPCs only when both are attracted to each other's sex.
  - Overture answers a player of the wrong sex with a refusal line.
- **Gameplay, not mechanics** (R-22's principle). It applies to scenes our mods start. A scene started from
  the AAF menu stays free.
- The player has no orientation (R-11). The player's choices are the player's own.
