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
