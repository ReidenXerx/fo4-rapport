# AAF's real API

Read out of the installed mod on 2026-09-17, not from memory or a wiki.

- Function list: the debug table inside `AAF_V1-7-4-1/Scripts/AAF/AAF_API.pex` (pex 3.9, gameID 2,
  little-endian). 56 functions, 40 of them public.
- Call shapes: AAF Sex 'Em Up 1.19 ships its own `.psc` sources, so the usage below is copied from
  working code rather than reconstructed.

## Getting the API object

```papyrus
AAF:AAF_API AAF_API = Game.GetFormFromFile(0x00000F99, "AAF.esm") as AAF:AAF_API
```

## Starting a scene

```papyrus
AAF:AAF_API:SceneSettings settings = AAF_API.GetSceneSettings()
settings.duration          = fDuration
settings.preventFurniture  = (bEnableFurniture == False)
settings.usePackages       = true
settings.skipWalk          = bSkipWalk
settings.isNPCControlled   = bNPCsControlPositionTrees
settings.position          = None
settings.furniturePreference = iFurniturePreference
settings.includeTags       = "..."      ; or None
settings.meta              = "AAF_SEU,Consensual"
AAF_API.StartScene(actors, settings)    ; actors is Actor[]
```

`meta` is a free-form tag string other mods read to tell whose scene it is. Ours must carry its own
marker so a scene we started is distinguishable from anyone else's.

## Events

Registered with `RegisterForCustomEvent(AAF_API, "<name>")`, handled as
`Event AAF:AAF_API.<name>(AAF:AAF_API akSender, Var[] akArgs)`.

`OnAAFReady` · `OnWalkInit` · `OnSceneInit` · **`OnSceneEnd`** · `OnAnimationStart` ·
`OnAnimationChange` · `OnAnimationStop` · `OnJoinAnimation` · `OnStageEvent`

Sex 'Em Up registers only five of these and never listens for `OnSceneEnd`, which is why it needs a
timer to decide a scene finished. We use the event.

## Event arguments, read off a real scene

AAF passes these straight through from its DLL, so they appear nowhere in its source. Captured
2026-09-18 from a scene Rapport started (John and Cathy, Diamond City).

`OnWalkInit` and `OnSceneInit` carry eleven:

| | |
| --- | --- |
| `[0]` | error code — 0 on success |
| `[1]` | actor array, as `[[Script <Ref (formid)>], ...]` |
| `[2]` | position, `None` when AAF chose it |
| `[3]` | **scene id** — the handle that ties every event of one scene together |
| `[4]` | location `[x, y, z, angle]` — all `-1.#IND` during the walk, real once the scene starts |
| `[5]` | **meta** — the string given to `SceneSettings.meta`, handed back verbatim |
| `[6]` | duration in seconds |
| `[7]` | bool |

`OnAnimationStart` carries fourteen, and the useful ones move:

| | |
| --- | --- |
| `[2]` | **animation name**, e.g. `(CHAK) Couch Cuddle B1` |
| `[3]` | **tag array**, e.g. `["LOVING", "SEUKissing", "", "NONSEX", "Couch", "KISSING", "NULLTOSELF", "AVILAS", "FOREPLAY"]` |
| `[4]` | meta |
| `[5]` | scene id |

Three consequences worth stating, because each replaces a guess:

- **Match on the scene id**, not on "there is only one request in flight". That hack was honest while
  `MaxConcurrentScenes` was 1 and wrong the moment it is not.
- **`meta` comes back verbatim**, so a scene of ours is identifiable as ours without tracking actors.
- **The tags say what kind of scene it was.** `NONSEX` and `FOREPLAY` mean a cuddle, and aftermath
  should produce nothing; that judgement needs no inference about what the animation implied.

## The 40 public functions

| Group | Functions |
| --- | --- |
| Scenes | `StartScene` `StopScene` `StopSceneWithAbruptStop` `QuickScene` `StartSceneByPosition` `ChangePosition` |
| Settings | `GetSceneSettings` `GetPositionSettings` `ChangeSetting` |
| Actors | `GetActorData` `SetActorLocked` `AssignRole` `RemoveRole` `ClearAllRoles` `GetActorTypeList` |
| Stats | `GetAttraction` `ChangeStat` `ChangeRelationshipStat` `ChangeStatMinimum` `ChangeStatMaximum` |
| Content | `FindMatchingAnimations` `GetPositionData` `GetFurnitureList` `GetProtectedEquipmentKeywords` |
| Appearance | `ApplyEquipmentSet` `ApplyEquipmentRules` `ApplyMorphSet` `ApplyOverlaySet` `RemoveOverlaySet` `ApplyMFGSet` `AddMFGBlock` `RemoveMFGBlock` |
| UI | `ShowBanner` `ClearBanner` `NarrationMessage` |
| Meta | `GetAPI` `GetVersion` `GetBuild` `GetAAFStatus` |

Three of these do work the design assumed we would have to build:

- **`SetActorLocked`** — AAF's own actor-busy lock. Two mods that both respect it cannot pick the
  same NPC. Ours must take it before a travel package, not when the scene starts.
- **`FindMatchingAnimations`** — asks whether a pair has any animation *before* we spend a 90-second
  walk on them. A pair with no content should never be selected.
- **`OnSceneEnd`** — the end signal, so cooldowns start from the truth rather than from a timer.

## StartScene stamps the actors busy, and only a finished scene clears it

`StartScene` is not a request AAF might decline quietly and forget. Its Papyrus half does this
before handing the call to AAF's DLL:

```papyrus
actors[I].AddKeyWord(AAF_ActorBusy)
```

and `SetActorLocked` does the same with `AAF_ActorLocked`. Both are ordinary keywords on the actor,
and both persist in the save. AAF refuses to animate an actor carrying either one.

So a request that never becomes a scene leaves the NPC flagged busy **forever**. Measured: one NPC
picked by three failed runs in a row stopped being usable by AAF at all, which then looked exactly
like AAF ignoring us for some other reason. `GetAAFStatus()` returning 2 (`AAF_ReadyStatus + 1`,
set when AAF's DLL reports ready) is what ruled out the alternatives.

Two consequences for anything driving AAF:

- **Check before asking.** `AAF:AAF_API` exposes `Keyword Property AAF_ActorBusy` and
  `AAF_ActorLocked`, so `akActor.HasKeyword(_api.AAF_ActorBusy)` is a cheap pre-flight. An actor
  carrying either is someone else's, or a casualty of an earlier failure.
- **Clean up after a failure.** `RemoveKeyword(_api.AAF_ActorBusy)` and `SetActorLocked(actor,
  false)` on every request that does not end in a real scene. AAF clears the flag itself when a
  scene ends properly, and only then.

Locking an actor *before* calling `StartScene` is therefore a deadlock rather than a reservation:
the flag that tells other mods "this one is taken" tells AAF the same thing.

## Appearance: overlays, morphs and expressions

Three API groups drive how an actor looks, and all three take a string id that is defined in XML
rather than in code:

| | |
| --- | --- |
| `ApplyOverlaySet(actor, id)` / `RemoveOverlaySet(actor, id)` | Skin overlays, through LooksMenu |
| `ApplyMorphSet(actor, id)` | Body morphs |
| `ApplyMFGSet(actor, id)`, `AddMFGBlock`, `RemoveMFGBlock` | Facial expressions |

An `overlaySetData` XML names LooksMenu overlay *templates*, and those templates live with the
textures in whichever mod provides them:

```xml
<overlaySet id="Belly">
  <overlayGroup duration="300" quantity="1">
    <overlay template="Belly_1" alpha="100" isFemale="true"/>
```

`quantity` picks that many at random from the group. **`duration` is AAF's own**, confirmed in
`common.xsd` as a float attribute of `overlayGroupType` — so a timed overlay needs no code, only an
XML set with the duration you want.

What it does not give you is persistence: an in-session timer does not survive a save, a reload or
the game closing mid-count, and an overlay stranded that way stays on the actor forever. Holding
`{actor, setID, expiresAt}` in game time, in a co-save, and removing it on a tick is the part a
framework adds.

### Assets are a separate thing from logic

On the reference setup the cum overlays come from CumOverlays v1.4, which bundles two unrelated
things: **assets** (`CumOverlays - Textures.ba2`, 57 templates in
`F4SE/Plugins/F4EE/Overlays/CumOverlays.esp/overlays.json`, `CumOverlays.esp` for LooksMenu to key
against, and `AAF/Cum_overlayData.xml` defining the sets) and **logic** (`CumOverlay_Main.pex`,
`CumOverlay_Starter.pex`, an MCM page).

The logic is replaceable; the assets are not, any more than an animation pack is. Treat such a mod
as a resource dependency and drive its sets yourself.

Coverage of those 57 templates: Anal, Back, Belly x5, Body, Breast x5, Butt x4, DP x4, Kidneys x2,
Vaginal x2, with male-body and mutant variants. **There is no face or mouth template among them**,
so anything aimed at the face needs a pack that provides one.

## The stat layer is empty, and that matters

AAF's schema (`Data/AAF/common.xsd`) supports `relationshipStat` with `isPersistent`, `decayRate`,
`minValue`/`maxValue` and optional backing by a real engine ActorValue
(`actorValueFormID` + `actorValueSource`).

Measured across all 180 deployed AAF XML files and the whole mod pool:

- **Zero `relationshipStat` definitions exist.** Not in AAF, not in any content pack.
- Only four actor stats exist at all — `Happiness`, `Fatigue` (Atomic Lust) and `Happiness`,
  `Fatigue`, `Arousal`, `Edge` (UAP) — every one declared `isPersistent="false"` with decay
  (UAP uses `decayRate="10000"`, an effectively instant reset).
- Scanning 12,120 compiled scripts (1,071 loose, 11,049 inside 239 BA2 archives), **no mod other
  than AAF itself calls `GetAttraction`, `GetActorData`, `ChangeStat` or `ChangeRelationshipStat`.**

So `GetAttraction` has nothing to return today. We define the stat ourselves: a persistent
Attraction relationship stat in our own `actorStatData` XML, written through AAF's API so it shows
in AAF's profile/scene UI and any other mod can read it — with our F4SE co-save as the
authoritative copy for need, cooldowns and refusal memory.

## Reading a .pex yourself

`tools/pexnames.py` dumps the function list from any compiled Papyrus script's debug table. No
decompiler needed, and it cannot be wrong about names the way a wiki can.

## AAF does not enforce the duration you pass it

`SceneSettings.duration` is handed to AAF's DLL and AAF's own `getDefaultSceneDuration()` fills it in
when a caller does not, so it looks like the scene length. It is not, or at least not reliably:

> Measured 2026-09-18. A scene requested with `duration = 30.0` was still running **three and a half
> minutes later**. `OnSceneEnd` never arrived, the actors kept their `AAF_ActorBusy` keywords, and
> the framework sat on "a scene is already running" until its own watchdog fired.

The registration name was not the problem, and this is worth stating because it was the obvious
suspect: `AAF_API` re-broadcasts with `SendCustomEvent("aaf:aaf_api_OnSceneEnd", akArgs)`, which is
exactly what the bridge registers for. AAF simply had not ended the scene, so there was no event to
send.

**So ending a scene is the caller's job.** AAF's own `MainQuestScript` does it when an actor walks
out of range:

```papyrus
AAF_API.StopScene(akObj2 as Actor, -1)    ; -1 is "all of it"
```

One actor is enough -- a scene is one thing, not one per participant -- and the proper stop is what
produces `OnSceneEnd`, releases the actors and clears the keywords. Releasing your own side while
AAF carries on leaves two NPCs animating with nobody watching them.

**Time the stop from `OnSceneInit`, not from the request.** AAF walks the pair to each other first,
and that walk is not the scene: request to scene start measured 12.5 seconds in an open market.

## includeTags is an AND, and failures come back down OnSceneInit

Two things learned the hard way on the first staged scene, within ninety seconds of each other.

**`includeTags` means "an animation carrying ALL of these", not "any of these".** Asking for five
alternatives gets you nothing, because nothing is all five at once:

```
[034] Failed to start 'FM' scene because there are no 'FEMALE HUMAN + MALE HUMAN' animations.
      Filters: includeTags (KISSING,MOUTHTOMOUTH,HANDJOB,HANDTOVAGINA,HANDTOPENIS)
```

So a set of alternatives has to be offered ONE AT A TIME. Rapport keeps a stage's list as options
and sends a single tag, moving to the next when AAF refuses.

**AAF reports refusals through `OnSceneInit` itself.** A real scene init carries **11** arguments; a
refusal carries **4** — an error level in `[0]` and the message in `[1]`. There is no separate error
event, and nothing about the event name says which one you have.

Treating a refusal as a start is a tight loop: the scenario restarts, asks for the same impossible
thing, is refused again. It ran about eight times a second and every pass logged `scene started`.
**Check the argument count before believing an event.**

The refusal is also better evidence than any up-front check. A tag index knows a tag exists
somewhere; it cannot know whether an animation exists for *this pair, in this furniture, here*. AAF
answering "no" is the only thing that does.
