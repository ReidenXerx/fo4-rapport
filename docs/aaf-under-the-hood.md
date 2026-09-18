# AAF under the hood

Everything here was measured against a running game or read out of AAF's own decompiled sources.
None of it is documented by AAF, and several items contradict what its API looks like it promises.
`docs/aaf-api.md` covers the call shapes; this file covers the behaviour underneath them.

Each entry says how it was established, because that is what makes it re-checkable when AAF changes.

---

## 1. AAF goes permanently deaf after a load, and it is a race

**The single most important thing in this file.** Roughly half of all save loads leave AAF unable to
run anything, with no error, no event, and no indication anywhere that something is wrong.

`AAF_MainQuestScript.EveryTime_Initialization` branches on `AAF_UpdaterQuestScript.getSWFPath()`:

- empty means *load the interface* — `UI.Load("HUDMenu", "root1", "AAF.swf", ...)`
- anything else means *reboot the one already at this path* — `ui.Invoke(path + ".reboot")`

That variable lives in the **save**, and the only thing that blanks it is the **updater's own**
`EveryTime_Initialization`. Both quests extend `AAF_QuestBase`, both register for
`OnPlayerLoadGame`, and Papyrus does not order delivery between them:

| Order | Result |
| --- | --- |
| updater first | path blanked → main sees `""` → `UI.Load` → **ready** |
| main quest first | reads the **stale** path → `.reboot` on a menu instance minted in a previous session, which no longer exists → **deaf for the session** |

**How it was established.** The same path came back byte-identical across four game launches and
many loads — `root1.instance166.instance165` — where a real `UI.Load` mints a new instance number
every single time. It is baked into the save and re-read forever.

**The cure**, verified 3/3 in game, recovering in about four seconds each time: call the
**updater's** `EveryTime_Initialization()` first, then the main quest's. The updater quest is form id
**132529** (`.esm` then `.esp`). It must also precede any `Stop()`/`Start()` of the main quest, or
the restart re-enters the same branch and buys nothing — measured: quest came back running, AAF
stayed deaf.

Blanking that path costs nothing when AAF is healthy; it is exactly what AAF writes there itself on
every load.

## 2. `GetAAFStatus()` is three states, not two

`AAF_API.psc:440`. It returns `AAF_ReadyStatus + 1`, **except** that it returns `0` when the main
quest is not running at all.

| Value | Meaning | Cure |
| --- | --- | --- |
| 0 | main quest stopped | `Start()` — a bigger act; the save may be losing AAF deliberately |
| 1 | running, interface never announced | §1 |
| 2 | ready | — |

`AAF_ReadyStatus` becomes 1 only when the string `"OnAAFReady"` arrives **back from the SWF**
(`AAF_MainQuestScript.psc:1140`). Readiness is announced by Scaleform, not by Papyrus.

## 3. There is no tree API — but a position names a tree

The word `tree` appears **nowhere** in AAF's entire Papyrus source set. Position trees run in the
SWF. So a tree cannot be started, steered or queried through the API.

It is reachable through the **position** layer instead: a `<position>` may carry
`positionTree="..."`, and positions are what `ChangePosition` and `StartScene` take. **Naming a
position is choosing a tree.**

Trees are where the staging lives. A branch carries `id`, `time` and sometimes `isExit="true"`:

```
Play Stage 1 -> Play Stage 2 -> ... -> Play Stage 6 -> Climax -> Finish
Stage 0 -> ... -> Stage 4 -> Orgasm -> Taste It -> Relax -> Finish
```

Measured on one real install: **83 trees, 42 ending in an Orgasm branch and 22 in a Climax branch**,
entered from 85 selectable positions, 64 of which reach a real ending. **72 of 83 carry
`strictExiting="true"`.**

## 4. A climax cannot be requested by tag

Every standalone climax position is hidden **on purpose**, because the tree is the intended route.
UAP's override file does it with two attributes:

```xml
<defaults ... loadPriority="1" isHidden="true"/>
<position id="BP70 Cowgirl Sequence 5" animation="Null" tags="...,Climax,..."/>
```

`animation="Null"` and `isHidden="true"` together retire the original pack's directly-selectable
climax positions. Asking AAF for `includeTags(CLIMAX)` correctly returns nothing.

## 5. `<defaults>` inherits, and missing it inverts your answer

An `isHidden="true"` on a file's `<defaults>` element applies to **every position in that file**. Read
positions without it and a whole pack reads as selectable when none of it is.

This produced a confidently wrong count here — 48 "visible" climax positions that were all hidden —
and that wrong count was then used to contradict AAF, which was right. **Any census of AAF's XML
must fold `<defaults>` and must drop `animation="Null"`.**

## 6. `ChangePosition` cannot leave the scene's furniture

`PositionSettings` is `{duration, position, includeTags, excludeTags, combinedTags}` — no field for
furniture or location. The SWF decides from the scene's current state, and a scene that began on a
desk cannot be moved to a `NoFurn` position.

Moving means **ending the scene and starting another** with `preventFurniture = true`, which is also
what it looks like in game: they get up and walk.

## 7. `StopScene` is asynchronous

Stopping and starting in the same breath earns:

```
[088] Failed to join actor to scene because that actor is already part of a currently running scene
```

**Measured:** the refusal landed at `01:26:53.758` and the old scene ended at `01:26:54.508` — three
quarters of a second later. `OnSceneEnd` is the signal that the actors are free; wait for it.

The busy keywords are the thing that actually blocks the new scene, so clear them on both actors
before restarting.

## 8. AAF never ends a scene that has no tree

The `duration` handed to `StartScene` is **ignored**. A tree leaves through its own `isExit` branch;
anything else runs until somebody calls `StopScene`, and until then both actors stay flagged busy —
for the rest of the save.

So a caller needs a deadlock breaker. It is not a scene length: imposing one takes the **ending**,
because the ending is the last thing to happen.

## 9. Refusals arrive down `OnSceneInit`, with 4 arguments

A real scene init carries **11**; a refusal carries **4** — error level in `[0]`, message in `[1]`.
There is no separate error event. Treating a refusal as a start is a tight loop: measured at roughly
eight restarts a second, every one logging "scene started".

## 10. `includeTags` is an AND

Asking for five tags asks for one animation carrying all five, which nothing is. A list of
alternatives has to be tried one at a time.

## 11. `GetPositionSettings()` presets the player's own exclusions

It returns `excludeTags = "default_excludetags"` — a sentinel the SWF resolves to the player's
configured list (it appeared as `POSE,UTILITY` on this install). Writing an empty string over it
throws their settings away. Only assign `excludeTags` when you actually have some.

## 12. `BSFixedString` interns case-insensitively — mixed case in logs is a mirage

Hand it `"handjob"` and it returns `HANDJOB` if AAF interned that spelling first, out of its own
scene tags. Our logs showed `kissing`, `PenisToVagina` and `HANDJOB` all sent from one lowercased
list, which looked like a serious bug and is not: the pool is shared and case-insensitive, and AAF
receives the same tag either way.

## 13. The scene's own tags come back on `OnAnimationStart`

`args[3]` is the full tag list of the position actually playing, in AAF's own casing — the only
reliable way to know what a scene *is*, including its furniture (`Desk`, `Couch`, `DoubleBed`,
`NoFurn`) and its composition (`F_M`, `M_M`, `F_F`, females first; there is no `M_F`).

## 14. `UI` is F4SE's, and `UI.Load` needs the menu to exist

`UI.psc` ships with F4SE (`Data/Scripts/Source/UI.psc`), not with the game, which is why it is in no
vanilla BA2. `UI.Load` puts an asset **inside** a menu that must already be open — so anything
driving Scaleform, including every restart of AAF, silently does nothing when `HUDMenu` is absent.

## 15. OPEN: `ChangePosition` has never once succeeded

**26 requests, 26 refusals**, across every stage, every scene, on furniture and off it, inside trees
and outside — for tags whose content demonstrably exists (5 selectable female+male `kissing`
positions; 273 female+male positions overall). AAF answers *"there are no FEMALE HUMAN + MALE HUMAN
animations"* every time, while `StartScene` matches the same pair happily.

Refuted so far: furniture (§6 — refused on `NoFurn`, outside any tree), the default exclusions (§11 —
1 of 273 positions carries `POSE`/`UTILITY`), and string case (§12).

Being measured with `FindMatchingAnimations(actors, queryID, includeTags, excludeTags,
combinedTags)`, whose reply arrives on `OnAnimationQueryResult` — passed straight through from AAF's
DLL, so its argument layout is not readable in any source and has to be learned from a running game.

**Until this is settled, treat tag-driven position changes as unproven.** Everything that has
actually worked came from AAF's own trees running themselves.
