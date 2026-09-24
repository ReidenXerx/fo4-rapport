# AAF — source of truth

What AAF actually guarantees, in our own words, reconciled from **the author's own documentation**
(Dagobaking's AAF wiki, `moddingham.com`) rather than from our measurements. Where the two disagree,
**the wiki wins and our measurement was a symptom**.

This is a working reference for Rapport and Chemistry, not a copy of the wiki. AAF and its
documentation are **copyright Dagobaking, all rights reserved** (`public/CopyrightNotice`); nothing
here is reproduced from it. Every claim cites the page it came from so it can be checked at source,
and anything we have not verified is marked as such.

Companion documents: [`aaf-under-the-hood.md`](aaf-under-the-hood.md) is our *measured* findings with
the author's review on top; this file is what the documentation says. Read this one first.

Reconciled 2026-09-22 against a full sweep of 121 wiki pages. Wiki version context: AAF 1.7.6.

---

## 0. The rules that change what we do

Everything below is detail. These are the ones that touch code we have already written.

| # | Rule | Where it bites us |
| --- | --- | --- |
| **A** | Empty tag fields are the literal string **`"NONE"`**, not `""`. The published signature of `FindMatchingAnimations` has `includeTags = "NONE"`, `combinedTags = "NONE"`, `excludeTags = "default_excludetags"` as its *defaults*. | Our `ChangePosition` calls pass `""`. This is the author's §15 note, and now it is visible in a signature. |
| **B** | **`FindMatchingAnimations` exists** (since 1.6.1): ask whether an actor set + tag combination matches anything, **without starting a scene**. Async; the answer arrives on `OnAnimationQueryResult` with your own `queryID` echoed back, carrying `matchCount` and the first matching position id. **Verified working here 2026-09-22** — see the measurement below. | Rapport learned "nothing matches" by starting a scene and catching `OnSceneInit` reason 4. This is the preflight we hand-rolled around. |
| **C** | `includeTags` is **ANY-of**, `combinedTags` is **ALL-of**, `excludeTags` is **any-of-excludes** and defaults to the ini's `pose,utility`. | Confirms §10. Our one-tag-at-a-time loop was over-cautious, never wrong. |
| **D** | **`OnAAFReady` fires a second time** when a user upgrades AAF inside an existing save — AAF reboots and re-announces. | Our readiness latch must be re-entrant, and the watchdog must not read a second ready as a fault. |
| **E** | Several API calls **do nothing before boot completes** and AAF says so with error `[072]`. `FindMatchingAnimations`, `GetPositionData` and `GetActorTypeList` name this explicitly. | Everything we send must be behind the ready gate, not behind a timer. |
| **F** | **`AAF_ActorBusy` is AAF's flag**, set when an actor enters a scene and cleared when they are no longer actively in one. `AAF_ActorLocked` is the *courtesy* flag for mod authors and **AAF does not enforce it**. `AAF_ActorBlocked` makes AAF ignore an actor entirely. | Confirms §7: we removed our keyword-stripping. It also means checking `AAF_ActorLocked` before we touch an actor is on us — nothing stops another mod ignoring it. |
| **G** | A positionTree's auto-created position carries **only the root branch's tags** unless the tree sets `combineTags="true"` (1.7.6). | Tag-based selection can silently never reach a later stage of a tree. Explains selection surprises better than any theory we had. |
| **H** | A branch's `time` overrides the scene `duration`; with no `time`, the branch uses the `duration` we sent; with neither, the ini's `default_scene_duration`. | Our "duration is ignored" finding was measured on a tree. **Confirmed in game 2026-09-22** — a timed non-tree scene ended itself in 29.83s against a `duration` of 30, with no `StopScene` from us. §8 was right and we were wrong. |
| **I** | The native plugin writes its own log to `Documents\My Games\Fallout4\F4SE\aaf.log`, and the wiki asks for it in bug reports. | Our tester bundle collects `f4se.log` but not this. Cheap to add, and it is the first place to look when AAF does not start at all. |
| **J** | `ChangeSetting` is **deprecated as of 1.7.5** and slated for removal; per-save MCM overrides replace it. `GetAttraction`, `AssignRole`, `ClearAllRoles`, `RemoveRole` are **legacy — do not use**. | We use none of these. Keep it that way; the attraction layer stays ours. |

---

## 1. Lifecycle — when AAF is listening

- **`OnAAFReady`** means boot finished: XML loaded and processed. It carries the full list of setting
  names and their values (two `Var`-packed arrays in the same order), so a mod can read the user's
  whole `AAF_settings.ini` without parsing a file.
- It **can fire twice in one save** (rule D above).
- Calling into AAF before it fires is ignored on purpose, to protect stability, and reported as
  `[072]`. Some functions will return blank or stale data rather than erroring — `GetActorTypeList`
  is documented as doing exactly that.
- `GetVersion()` returns an int with the dots removed (1.7.4.1 → `1741`); `GetBuild()` returns
  `"Alpha"`, `"Beta"` or an **empty string for a final release**.
- `GetAPI()` is a global that hands back the API script from anywhere.

Sources: `authors/API/OnAAFReady`, `authors/API/GetVersion`, `authors/API/GetBuild`,
`authors/API/GetAPI`, `authors/API/GetActorTypeList`, `authors/ErrorIndex` [072].

## 2. Starting a scene

`StartScene(Actor[] actors, SceneSettings settings)` is the current entry point. `QuickScene` and
`StartSceneByPosition` still work but the wiki asks for `StartScene` instead.

Build the settings with **`GetSceneSettings()`** — it returns the struct pre-filled with defaults, so
you only touch the fields you mean to change. Same for `GetPositionSettings()`.

The fields that matter to us, with their shipped defaults:

| Field | Default | Meaning |
| --- | --- | --- |
| `duration` | `default_scene_duration` (ships 30) | Seconds. `-1` = until something stops it. |
| `position` | none | Exact position id; omit and AAF picks a compatible one at random. |
| `includeTags` | none | **ANY** of these tags → included. |
| `combinedTags` | none | **ALL** of these tags → included. |
| `excludeTags` | `default_excludetags` (ships `pose,utility`) | ANY of these → excluded. |
| `preventFurniture` | false | Drop furniture positions from the pool. |
| `furniturePreference` | `-1` → ini's 75 | Percent chance furniture wins over ground. |
| `usePackages` | true | False means *we* own the AI packages. |
| `skipWalk` | false | Skip the walk; packages still apply. |
| `locationObject` / `scanLocation` / `scanRadius` | none / none / ini's `furniture_search_area` | Where the scene happens and where furniture is looked for. |
| `ignoreActorLocations` | false | Stop the actors' own ground spots being candidates. |
| `isNPCControlled` | false | A tree auto-advances with no player widget. |
| `playOnce` | false | Play once and end. **No effect on trees.** |
| `isRootLocked` | false | The End key cannot stop it; the API still can. |
| `ignoreCombat` | ini's true | False ends the scene when an NPC must fight. |
| `keepPowerarmor` | ini's false | Leave the player in power armour. |
| `meta` | none | An opaque string of ours, echoed back on every event of that scene. |
| `formation` | none | **Creates a separate scene per actor** with these settings. |
| `startEquipmentSet` / `stopEquipmentSet` | none | Override the XML's sets; a comma-separated list stacks in order (1.7.6). |
| `megaScene` | none | A megaScene id. |

**Joining is implicit**: call `StartScene` with a set of actors where one is already animating inside
a parent position, and AAF joins rather than starting fresh. Without a declared parent it keeps a
history, so when the joiner leaves the original actor returns to what they were doing.

`meta` is the clean way to tag our own scenes. We should be using it more than we do.

Sources: `authors/API/StartScene`, `authors/API/SceneSettings`, `authors/API/GetSceneSettings`,
`authors/API/QuickScene`, `authors/API/StartSceneByPosition`.

## 3. Stopping and changing

- **`StopScene(actor, levels = -1)`** — any actor in the scene will do. `-1` (the default) ends the
  whole stack; `1` ends one level and lets a parent position carry on. Our `StopScene(actor, -1)` is
  right.
- **`StopSceneWithAbruptStop(actor, levels)`** is the companion that drops the actors out
  immediately, the way a combat interrupt does.
- **`ChangePosition(actor, PositionSettings)`** switches the animation inside a running scene. With
  no `position` set, AAF picks a compatible one honouring the same include/exclude/combined fields.
  `PositionSettings` is the small struct: `duration`, `position`, `includeTags`, `excludeTags`,
  `combinedTags` — nothing else.

Sources: `authors/API/StopScene`, `authors/API/ChangePosition`, `authors/API/PositionSettings`.

## 4. Events — the shapes worth knowing

Registration is plain `RegisterForCustomEvent(AAF_API, "OnSceneInit")` and so on; every event page
shows that form. Payloads are `Var[]` with `Utility.VarToVarArray` for the packed members.

**Order of the run:** `OnSceneInit` → `OnWalkInit` → `OnAnimationStart` → (`OnAnimationChange` /
`OnStageEvent` / `OnTransitionStart` …) → `OnAnimationStop` → `OnSceneEnd`.

- **`OnSceneInit`** fires once every actor has locked — or **earlier, on error**. `akArgs[0]` is the
  reason code, and this is the complete list:

  | | | | | |
  | --- | --- | --- | --- | --- |
  | 0 healthy | 1 lockingFailed | 2 incorrectQuantity | 3 noLocationAnimations | 4 noActorAnimations |
  | 5 missingPosition | 6 actorBusy | 7 actorInvalid | 8 actorInScene | 9 noValidActors |
  | 10 sceneNotFound | 11 actorExiting | | | |

  3 and 4 are different failures: *nothing fits here* versus *nothing fits these actors at all*.
- **`OnWalkInit` only fires when `OnSceneInit` reported no error** — so its absence is not a hang,
  it is a refusal we should already have seen.
- **`OnAnimationStart`** is the looping animation beginning, after travel, locking and any entrance
  transition. **`sceneActors` is ordered to match the animation's actor slots** — index 0 is slot 0.
  That is the documented guarantee our slot-0 convention rests on.
- **`OnAnimationChange`** is position-level: stage changes inside an animationGroup do *not* fire it.
  Since 1.7.6 its `sceneActors` is re-ordered for the new animation; **before 1.7.6 it still
  described the pre-switch order**, so on an older AAF that ordering cannot be trusted after a
  switch.
- **`OnStageEvent`** is opt-in per stage (`sendEvent="true"` on the stage node). Since 1.7.6 its tags
  and idle forms describe the stage that fired, not the group's first stage.
- **`OnTransitionStart`** (1.7.6) is the only sight of a transition; `fromID`/`toID` are the animation
  ids, with the literal `"none"` marking an entrance or an exit. The engine may re-capitalise it, and
  Papyrus string comparison is case-insensitive, so compare plainly.
- **`OnSceneEnd`** fires when the scene is over **and every actor is unlocked** — that, not
  `OnAnimationStop`, is the moment an actor is free. It also carries an **action log** in
  `akArgs[10..13]`: four parallel arrays of source actor, target actor, action id and seconds
  applied. We have never read it.
- `idleForms` on the animation events is empty unless `api_include_idle_forms` is on.
- **`OnAnimationQueryResult`** answers `FindMatchingAnimations`: your `queryID`, a `matchCount`, and
  the first matching position id (empty when the count is zero).
- **`OnActorData`** answers `GetActorData`: actor, a status string from
  `$WALKING / $ANIMATING / $LOCKING / $UNLOCKING / $LOCKED / $UNLOCKED / $WAITING`, and the actor's
  stat names and values as parallel arrays.
- **`OnPositionData`** answers `GetPositionData` with an inverted flag: **`0` means installed**, `1`
  means not.
- **`OnStatEvent`** fires when a stat crosses a range declared in reaction XML, and names both the
  actor who owns the stat and the actor who changed it.

The `doppelganger` slot in `OnSceneInit`, `OnJoinAnimation` and `OnWalkInit` is always `None` — a
retired body-double system. Ignore it.

Sources: the sixteen `authors/API/On*` pages.

## 5. Actors — keywords and locking

- `AAF_ActorBusy` — AAF's own. Applied on entering a scene, removed when the actor is no longer
  actively in one. **Not ours to strip.**
- `AAF_ActorLocked` — the mod-author flag. `SetActorLocked(actor, bool)` adds or removes it and
  returns true only if the state actually changed (false means it was already that way). The wiki is
  explicit that **AAF does not enforce it** — "do the right thing" is the whole enforcement. So we
  check it before recruiting an actor, and we set it while we are staging one.
- `AAF_ActorBlocked` — AAF ignores the actor completely, or throws an invalid-actor error.
- `AAF_GenderOverride_Male` / `_Female` — override the vanilla gender for animation matching.
- `AAF_BlockMFG_All` / `_Mouth` — suppress AAF's facial morphs (the mouth one is morph id 2).
  `AddMFGBlock` / `RemoveMFGBlock` do the same per morph id list.

Sources: the `authors/API/AAF_Actor*` and `AAF_BlockMFG_*` pages, `authors/API/SetActorLocked`.

## 6. Stats, overlays and morphs — the layer we are building on

AAF carries a per-actor stat system with a real API: `ChangeStat(actor, statID, value)`, plus
`ChangeStatMinimum` and `ChangeStatMaximum`. Stats are declared in `actorStatData` XML and can be
hidden from the UI.

The part worth our attention: **a `morphSet` or an `overlaySet` can bind to a stat id**, with
`statMin`/`statMax` mapping the stat linearly onto morph strength or overlay alpha (clamped outside
the range; the alpha formula is the overlay's own alpha scaled by that fraction). A stat defaults to
the stat's own declared min and max if the set omits them, and several sets can bind to one stat.

That is a declarative route from a number we own to something visible on the actor, with no Papyrus
in the loop per frame. If the Rapport attraction stat is ever to *show*, this is the mechanism —
and it needs LooksMenu, because without it AAF disables morph handling entirely.

Two constraints: AAF will not apply the same overlay twice to one actor, and an overlay must still
be applied by normal means — the stat binding only drives its alpha.

`ApplyEquipmentSet`, `ApplyMFGSet`, `ApplyMorphSet`, `ApplyOverlaySet` and `RemoveOverlaySet` apply
XML-declared sets by id from Papyrus. **The wiki does not say whether these are reverted at scene
end** — our sweep looked and found nothing, so treat cleanup as ours until tested.

Sources: `authors/API/ChangeStat*`, `animators/XML/morphSetData`, `animators/XML/overlaySetData`,
`animators/XML/actorStatData`, `authors/API/Apply*`.

## 7. Narration and banners — AAF's own HUD surface

`NarrationMessage(string)` takes either the id of a narration node from XML or, failing that, a raw
string to display. `ShowBanner(bannerID)` / `ClearBanner()` drive banner XML, where a duration of
`-1` holds the banner until it is cleared or replaced. `narration_duration` (ini, 5000 ms) is how
long each narration phrase sits on screen.

Worth knowing because our Narrator builds its own HUD line. Ours stays — it prints numbers AAF has
no notion of — but for a plain line of text this is the native surface, and a narration node that
expects actor names will come out blank through the API, because the call has no scene to draw them
from.

Sources: `authors/API/NarrationMessage`, `authors/API/ShowBanner`, `authors/API/ClearBanner`,
`animators/XML/narrationData`, `animators/XML/bannerData`.

## 8. positionTrees — what actually controls a staged scene

We read trees wrong for months. The documented model:

- A tree's `positionConfig` auto-creates the position; dedicated `position` XML is legacy.
- **`time` on a branch** is the seconds that branch plays when the tree is NPC-driven. No `time` →
  the scene's `duration`. Neither → the ini default.
- **`isExit`** ends the scene after that branch plays — once through by default, or looped if the
  exit branch also has a `time`.
- **`strictExiting`** stops an NPC-driven tree exiting merely because it ran out of options; it keeps
  going until it reaches an exit node.
- **`forceComplete`** locks the navigation widget so the animation cannot be interrupted.
- **`isFlavor`** plays once without advancing navigation, then drops back to the current loop.
- **`combineTags`** (1.7.6) — see rule G. Off by default, and the reasoning is stated: a scene may
  never get past the first stage, so the tree advertises only what it starts as.
- **`useConditions`** hides branches until their conditions are met, live during the scene.
- **`npcControlWhenPCActor`** forces NPC control when the player lands in listed actor slots.
- Start/stop effect sets resolve **most-specific-first**: the playing position, then the
  animationGroup, then the tree. The fallback only fills what is unset — it never overrides.
- Inside an animationGroup, `treeOutcome="back"` / `"advance"` (with `treeTarget`) is the
  tree-aware navigation; `switchTo` leaves the tree context entirely. A stage with no `treeOutcome`
  is neutral and the group keeps looping.

Sources: `animators/XML/positionTreeData`, `animators/XML/positionData`,
`animators/XML/animationGroupData`.

## 9. Tags — where they come from, and in what order

A position's effective tags are assembled, not declared in one place:

1. the position's own `tags`,
2. plus the tags rolled up from the animation's **actions** (the documented way to avoid repeating
   yourself),
3. plus/minus a furniture group's `addTags` / `removeTags` (1.7.6) when the position sits in that
   group,
4. then a `tagData` file's `tags` (added, or replacing when `replace="true"`),
5. and finally `tagData`'s `removeTags` — **applied last, and it wins over everything above**.

So a tag can be present in the XML we are reading and still not be on the position at runtime.
When tag-based selection surprises us, this order is the first thing to check.

**Measured here (2026-09-24, R-25): the non-sex markers.** A survey of `positionData` tags plus
every `*_tagData.xml` found two markers pack authors use for "this is not sex":
- `NonSex` is CHAK's: 70 position ids in `CHAKPack_tagData.xml`, 39 of them installed here (hugs,
  cuddles, kisses, snoozes, a slow dance). The rest name positions of CHAK packs this install lacks.
  None of them carries a sex-act tag.
- `SFW` is UAP's, on 11 installed positions: Atomic Lust's embrace, kiss, cuddle and holding hands,
  F_M and M_M. None carries a sex-act tag.
- `Kissing` is not a marker: at least 16 installed positions carrying it also carry an act
  (`PenisToVagina`, `Cunnilingus`, ...).
- About ten kiss-only positions carry no marker at all: BP70's and UAP's Kissing, Make Out, Smooching,
  Rufgt's Gay Kissing and others.

A survey of `positionData` alone reported **zero** `NonSex` positions. The tag lives only in the
tagData file, so a probe that reads one of the two sources is wrong, not merely incomplete.

Sources: `animators/XML/tagData`, `animators/XML/furnitureData`, `animators/XML/actionData`,
`animators/XML/animationData`.

## 10. Settings that shape what we see

Read from `AAF_settings.ini`; the highest `priority` file wins when several exist. Since 1.7.5 the
MCM stores **per-save overrides** on top, so a value we read from the ini is not necessarily the one
in force — and a setting the player never touched keeps following the ini live.

| Setting | Ships | Why we care |
| --- | --- | --- |
| `default_scene_duration` | 30 | The duration behind every `-1`-free call. |
| `default_excludetags` | `pose,utility` | What is filtered out when we say nothing. |
| `actor_search_area` | 1500 | Scan radius for actors, in the wizard **and through the API**. |
| `furniture_search_area` | 1500 | Same for furniture. |
| `distance_limit` | 10000 | **AAF stops any scene when the player moves this far from it.** |
| `walk_timeout` | 10 | Give up walking and animate in place. |
| `reequip_delay` / `stopmorph_delay` | 15 / 15 | Cleanup does not happen at `OnSceneEnd` — it trails it. |
| `default_furniture_preference` | 75 | Furniture beats ground three times in four. |
| `default_ignorecombat` | true | Scenes survive nearby combat by default. |
| `max_array_size` | 2000 | Exceed it and AAF reports `[071]` rather than crossing to Papyrus. |
| `api_include_idle_forms` | false | Why `idleForms` is empty in our handlers. |
| `troubleshooting_level` | 0 | Which errors reach the player as pop-ups. |
| `debug_to_papyrus_log` | false | Sends AAF's own messages to `Logs/Script/User/`. |
| `reload_xml_on_game_load` | false | XML is read **once at the main menu** and reused all session. |

`distance_limit` and `reequip_delay` together explain a family of "AAF did not clean up" reports
that are simply AAF taking its time on purpose.

**The merge is per SETTING, not per file.** `Rapport_settings.ini` sits at priority 100 and sets two
debug switches, and every other setting still comes from `AAF_settings.ini` (priority -1). A file
without a `priority` line counts as 0. The files are `Data/AAF/*_settings.ini`.

**`SceneSettings.excludeTags` REPLACES `default_excludetags`; it does not add to it.** The factory
holds the sentinel string `default_excludetags`, which AAF resolves on its side and which cannot be
extended in place. So a caller that excludes anything must carry the defaults itself, or `pose` and
`utility` positions come back. Rapport reads them from the settings files (R-25). A value the
player changed in AAF's MCM is per save and invisible from disk.

Source: `authors/API/AAF_settings`.

## 11. Error codes we will actually meet

The full index is on the wiki; these are the ones that land on us.

| Code | What it means for us |
| --- | --- |
| `034` | No animation matched — **and the message prints the include, exclude and combined tag lists it used.** This is the refusal text to capture when something is rejected. |
| `044` | Animations exist for these actors but none fits the target location. |
| `060` | The actor is busy and cannot start a scene. |
| `072` | We called before boot finished; the call was dropped to protect stability. |
| `071` | An array crossing to Papyrus exceeded `max_array_size`. |
| `085` | The animation is shorter than the engine's minimum; AAF pads around it. A pose should carry the `pose` tag. |
| `086` / `087` | A referenced id was not found / a null id arrived. Since 1.7.5 these replace most of the per-type "id not found" codes. |
| `088` / `092` / `093` | Join refused: already in a scene / the scene ended a moment earlier / an actor is on the way out. |
| `027` | Wrong actor count for the position — it reports expected and actual. |
| `101`–`106` | positionTree and staged-scene warnings. **They used to be `090`–`095`**, shared with unrelated messages, before 1.7.5 — so an old log's code numbers cannot be read against the current index. |

Source: `authors/ErrorIndex`.

## 12. Platform facts

- AAF ships **both** native plugins and loads the one matching the runtime: `aaf_1_10_163.dll`
  (Old-Gen, F4SE 0.6.23) and `aaf_1_11_221.dll` (Anniversary Edition, F4SE 0.7.8 + Address Library).
  Each rejects itself on the wrong runtime.
- **The Next-Gen builds 1.10.980 / 1.10.984 and the older AE 1.11.191 are not supported.**
- **Scene cameras and first-person POV are Anniversary Edition only**, and AAF disables them on
  Old-Gen regardless of the setting.
- LooksMenu is not required but without it AAF turns off all body-morph handling and facial
  expressions.
- The native log is `Documents\My Games\Fallout4\F4SE\aaf.log` (rule I).
- AAF alters no game forms; its documented conflict surface is the **crosshair and compass UI
  layers**, which its own UI depends on.

Sources: `public/requirements`, `public/CHANGELOG`, `public/Troubleshooting`,
`public/RequirementsInstallationandCompatibility`, `authors/TheAAFEcosystem`.

---

## Known stale page

`public/RequirementsInstallationandCompatibility` still says morph copying is disabled pending a
LooksMenu fix — a statement about the **body-double** system. `public/compatibility` says that system
is retired and the player is animated directly. The second is current; the first is a leftover, and
it is the same retired system as the always-`None` `doppelganger` event slot. Flagged rather than
resolved, because it is the author's to correct.

## What this sweep could not answer

Honest gaps, so nobody reads silence as a guarantee:

- Whether `ApplyEquipmentSet` / `ApplyMorphSet` / `ApplyOverlaySet` changes are reverted when a
  scene ends. Not documented; assume they are ours to undo.
- Whether events are broadcast to every registered listener or only to the mod that started the
  scene. Never stated. Our handlers already assume the broad case and filter on `meta`, which is the
  safe reading either way.
- Default minimum and maximum for AAF's own shipped stats.
- Whether a timed scene with no tree ends itself (§8). Documented as yes by implication; **untested
  by us in game.**

## Measured here — tag matching works

Recorded because this repo asserted the opposite for months, in four places.

**2026-09-22, AAF 1741 (1.7.4.1 Beta)**, through the `query` dev verb, Magnolia `0002268B` +
Randall Chase `001D1F49`, `excludeTags = "default_excludetags"`:

| include | matches | first position |
| --- | --- | --- |
| `Kissing` | **28** | (CHAK) *Staged* Bar Kisses |
| `PenisToVagina` | **38** | DR wodhorse 01 |
| `Foreplay` | **39** | (CHAK) *Staged* Bar Kisses |
| `Hugging` | **5** | (CHAK) *Staged* Hugs |
| `Aggressive` | **9** | [UAP] BP70 - Kinky Extractor Chair 1 |
| `Standing` | **1** | [UAP] BP70 - Standing 69 |
| `Oral` | 0 | — |

Three controls, each returning the identical count:

- **`combinedTags` `""` vs `"NONE"`** — no difference (28 both ways). So passing `""` is a real
  deviation from the documented contract and is **not** the cause of anything we measured.
- **idle vs mid-scene** — no difference. Asked while the same two were animating: 28 and 38 again.
- **`"Kissing"` vs `"KISSING"`** — no difference, confirming the author's note that tags are
  case-insensitive.

`Oral` returning 0 is most likely a tag name the installed packs do not use, not a failure: the
other six all answer.

**What this retracts.** "FindMatchingAnimations returned 0 for all 17 tags queried" and "AAF's tag
matching is what does not work" were load-bearing in `CLAUDE.md`, `src/Config.h`, `src/Orders.h` and
this project's reasoning about `ChangePosition`. They are wrong. AAF can see this content.

**What it does not settle.** `ChangePosition` was still refused 26 times out of 26, and that is a
different call. Its refusal is now **unexplained** rather than explained — which is the honest
position, and better than a wrong explanation. Why the original probe measured 0 is also unknown;
the tags it sent were real, its exclude lists were narrow, and neither control above reproduces it.

## Measured here — ChangePosition, three ways

**2026-09-22, AAF 1741**, same pair, mid-scene, through the `changepos` dev verb. AAF's author asked
us to retry with `"NONE"` in the tag fields rather than `""` (issue #1 §15). All three arms were
refused, identically, with `OnSceneInit` status **4** (`noActorAnimations`):

| arm | what went in the tag fields | result |
| --- | --- | --- |
| `factory` | `GetPositionSettings()` untouched | refused |
| `none` | `includeTags`/`combinedTags` = `"NONE"` | refused |
| `empty` | `includeTags`/`combinedTags` = `""` | refused |

The refusal, verbatim:

```
[034] Failed to start 'FM' scene because there are no 'FEMALE HUMAN + MALE HUMAN' animations.
Filters: includeTags (), excludeTags(POSE,UTILITY), combinedTags().
Install animation pack with this type of animation. [filterMulti returned 0]
```

with `includeTags (NONE) ... combinedTags(NONE)` in the other two arms. So `"NONE"` and `""` reach
AAF distinguishably and **neither changes the outcome** — §15 is not what was wrong with our calls.

**Two things worth the author's attention.**

1. **His own factory already sends what §15 recommends.** `GetPositionSettings()` fills `position`,
   `includeTags` and `combinedTags` with Papyrus `None`, and those arrive rendered as `NONE` in the
   error text. A caller using the documented factory never sends `""` in the first place.
2. **Two matchers inside AAF disagree about the same pair.** `filterMulti` returns **0** for
   `FEMALE HUMAN + MALE HUMAN` with no include filter and only the default excludes — while
   `FindMatchingAnimations`, same two actors, same `default_excludetags`, tested both idle and while
   this very scene ran, returns **28** for Kissing and **38** for PenisToVagina.

**The tree explanation is ruled out.** That control has now been run. A scene started on a *named*
position with no tree (`[UAP] BP70 - Standing 69`, via the `startpos` dev verb) is refused exactly
the same way — same `[034]`, same `filterMulti returned 0`, same status 4. So `ChangePosition` is
refused whether or not the scene runs a positionTree, and "a tree owns its navigation" explains
nothing here.

What remains is the plain discrepancy: **`filterMulti` says 0 for a pair that
`FindMatchingAnimations` says 28-39 for**, same actors, same default excludes, same session.

One correction to our own reading: the `OnAnimationChange` events that follow a refused
`ChangePosition` (Spooning 02 → Spooning 03) are the **tree advancing on its own**, not our call
landing late. The call did nothing.

## Measured here — a timed scene without a tree ends itself

The other thing AAF's author corrected us on (§8), and the one we had no way to test until the
`startpos` dev verb existed.

**2026-09-22.** A scene started on the named position `[UAP] BP70 - Standing 69` — no positionTree —
with `SceneSettings` straight from `GetSceneSettings()`, so `duration` was the ini default of 30:

```
21:28:21.931  OnAnimationStart
21:28:51.761  OnAnimationStop     <- 29.83s later
21:28:52.378  OnSceneEnd
```

**Rapport issued no `StopScene`.** The scene was started outside its own bookkeeping precisely so
that nothing of ours could end it, and the log carries no stop.

So `duration` is honoured, AAF ends the scene itself, and our recorded finding that "duration is
ignored, the caller must always `StopScene`" was measuring a **tree** — where a branch's `time`
governs instead. Exactly what the author said.

**What this does and does not mean for Rapport.** Rapport times every scene itself and stops it by
hand, and on a non-tree scene it does not need to. But Rapport lets AAF *choose* the position, and
both scenes watched during this session ran trees or staged groups — the branch advanced on its own,
twice. On n=2 that is a hint, not a split. **Do not remove the manual stop on the strength of this
result**: in a tree the branch's `time` governs, `duration` is only the fallback, and the manual stop
is still what ends the scene. Measure how often Rapport's scenes are trees first.

## Measured here — GetActorData never answers

**2026-09-22, AAF 1741.** `GetActorData` is documented to answer on `OnActorData` with the actor,
a status string and that actor's stats. It does not answer at all.

| | |
| --- | --- |
| idle actor | call sent, **no `OnActorData` in 12s** |
| same actor mid-scene, scene confirmed by `OnAnimationStart` | **no `OnActorData` in 10s** |
| `aaf: OnActorData` lines in the entire log | **0** |

**The control is what makes this worth stating.** In the same minutes, on the same actor, Rapport
received `OnSceneInit`, `OnWalkInit`, `OnAnimationStart`, `OnAnimationStop` and `OnSceneEnd`. The
registration path, the mangled event names and the plumbing all demonstrably work; this one call is
ignored. Another mod's AAF addon measured the same silence independently, with its own registration,
and its neighbouring `GetPositionData` answers normally.

So `GetActorData` joins `ChangePosition` as an entry point that accepts a call and never replies. No
theory is offered for either.

**A counting trap worth remembering.** `grep -c OnActorData` returned 4 and every one was our own log
line saying *"the answer, if any, arrives as OnActorData"*. The real count was zero. When grepping a
log for evidence that something happened, match the line the RECEIVER writes, not the name of the
thing — the name appears in everything that mentions it, including the code that failed.

## How to use this file

Cite it the way you would cite the wiki. When our behaviour disagrees with a section here, the
section is right and the behaviour is a bug — unless we have a measurement that contradicts it, in
which case that measurement belongs in `aaf-under-the-hood.md` and in a question to the author, not
in a silent workaround.
