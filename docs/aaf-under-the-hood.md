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

## 16. `OnAnimationChange` is the stage-advance hook. `OnStageEvent` is silent.

Established by counting events across three complete scenes (2026-09-19).

`OnStageEvent` is registered by the bridge and fired **zero** times. The hook whose
name promises exactly this does not deliver it, and nothing says so.

`OnAnimationChange` fires **once per tree step**, and the count matched the
catalogue's stage count for all three trees -- 6 events for a 6-stage tree, three
times out of three. Scene three names them unambiguously: `Gay Spooning`, `02`,
`03`, `04`, `Orgasm (loop)`, `Orgasm (loop)`.

`OnAnimationStart` fires **once per scene**, on the entry animation only: 2 Start
events against 10 Change events. Anything that forwards only `Start` freezes on
the tree's opening position for the whole rest of the scene.

Both carry the same layout: `args[2]` the position name, `args[3]` the tag list.
No argument carries a stage NUMBER -- `args[0]`, `args[7]` and `args[8]` were
`0`, `0` and `False` on all 21 animation events -- so steps must be counted, not
read.

## 17. A tree's declared `time` is not wall-clock, and the ratio is not constant

One tree declaring 120s along its longest path ran **179.9s** in one scene and
**226.3s** in another: 1.50x and 1.89x. Two samples, two different multipliers, so
there is no calibration factor to apply. Treat the declared total as a LOWER BOUND
and never as a duration.

This is why the story advances on tree steps and not on a scaled clock. Scaling
stage seconds onto the declared length put the orgasm face 63 and 58 seconds ahead
of the orgasm animation in two scenes, then held it for the remainder.

## 18. "Reaches an orgasm" is a branch NAME. 27 of 61 have no climax tag at all.

`ending` is graded from a branch id containing "climax" / "orgasm" / "finish". Of
74 selectable entries, 61 grade as reaching an orgasm -- and only **34** have a
position tagged `climax*` anywhere in the tree, **32** at the exit position.

`rxl_bp70_impregnate_mish_Tree` is the proof: branches named `Orgasm` and
`Finish`, and all eleven positions it can reach carry the same tag list with no
climax among them. It ran a complete 226-second scene and never emitted a climax
tag. `Impregnate Cowgirl` and `Gay Spooning` both did.

So a branch name is a claim about the author's intent and the position tags are
the fact. Grade from the tags when the answer matters -- and note this is the same
trap as `isExit` (finding 11), in the opposite direction: there, two trees named a
branch "Stage 4 (No orgasm)" and a substring test read them as reaching one.

## 19. An AAF animation almost never carries its own facial expression

`mfgSet` is the facial layer and it hangs off `<action>` and `<animation>`, **not
off `<position>`** -- a census of position attributes finds zero and is simply
looking in the wrong place.

Across the whole install: 11 mfgSet definitions outside Rapport's own, and **10
references**, against 2,240 actions and 1,358 animations. Roughly 0.4%. The BP70
pack that most scenes here play references none.

AAF's `<morph>` / `<morphSet>` layer is a different thing and is **body only** --
`Erection`, `Penis Adjust`, `Anus Spread`, `VaginaPenetrate`, `NippleLength`. Not
one facial morph among 1,699 entries.

So a framework is the primary source of faces, not a fallback. The risk runs the
other way: Rapport's sets are declared `lock="true"`, which holds a morph against
anything else that would move it, so on those ~10 animations we would override a
face the author chose deliberately for that exact animation.

## 20. Do not touch equipment. `startEquipmentSet` REPLACES AAF's undressing.

Attempted 2026-09-19, reverted, and the code removed. Recorded so nobody tries it again
without knowing the cost.

**The goal.** A visible-holstered-weapons mod renders the weapon as an armour piece in a
biped slot. AAF's own `unEquip` set lists all twelve "Possibly Weapons" slots, so on any
scene AAF undresses, that rig comes off with the clothes. It does NOT come off on a scene
AAF leaves clothed -- a cuddle or a kiss -- so the rifle stays on through the animation.

**What broke it.** Passing `settings.startEquipmentSet = "<our set>"` to `StartScene` left
both actors **fully dressed through a complete sex scene**. Naming a start set replaces
AAF's automatic undressing rather than adding to it.

**And nothing said so.** AAF accepted the scene, the tree walked, every step and face was
correct, and neither Rapport.log nor Papyrus.0.log mentioned equipment at all. This is
visible ONLY on screen, which makes it the worst shape of failure: silent, and invisible to
every check a plugin can make about itself.

**Where the undressing actually comes from.** `AAF_settings.ini` has
`auto_equipment_on = true` -- "Automatically makes equipment changes based on action XML
tags and equipmentRules XML configuration". This install has NO `equipmentRules` file and
no `customEquipment` anywhere, so the automatic path is AAF applying its own `unEquip` set
from action tags. `startEquipmentSet` is a separate, explicit override of that decision.

Two more settings matter if this is ever revisited: `protect_custom_equipment = true` means
a start set will not unequip items with customEquipment states at all, and
`legacy_equipment_restore = false` means AAF restores from a **frozen copy** taken at scene
start -- so anything that changes equipment mid-scene is working against a snapshot that
has already been taken.

**Untested, and left that way deliberately.** `AAF_API.ApplyEquipmentSet(actor, setID)` is
a runtime call and is additive in principle -- it would leave `startEquipmentSet` alone. It
was written and never tested in isolation, because the override was still present in the
same build. So "ApplyEquipmentSet also breaks undressing" is NOT established; it is unknown.

**Two toolchain facts learned on the way.** `Actor.GetEquippedWeapon` returns a `Weapon`
and Fallout 4 ships **no `Weapon.psc`** -- the base sources have `Armor.psc` and `Form.psc`
and nothing for weapons -- so the held weapon cannot be read from Papyrus at all
("unknown type weapon", whatever you assign it to). And the base game has **no
`GetWornItem`**, only `UnequipItemSlot(int)`, so a mod that clears a biped slot itself can
never identify what to put back.

**`ApplyEquipmentSet` ends in `ui.Invoke`** via `AAF_API.sendEvent`, so it kills the calling
stack like every other AAF call: one actor per `CallFunctionNoWait`, or the second is never
reached. Confirmed in game -- both trace lines appeared, 33ms apart.

## 21. AAF can apply an overlay but cannot tint one, and nothing on disk paints a face

Asked for sweat and a blush during a scene, the first instinct is to drive somebody else's
overlays the way `Rapport_overlayData.xml` already drives CumOverlays'. Two measurements kill
that.

**The overlay vocabulary.** `Data/AAF/common.xsd`, `overlayType`:

```
<xs:complexType name="overlayType">
    <xs:attribute type="xs:byte" name="alpha"/>
```

`template`, `alpha`, `isFemale`. That is all of it. AAF applies a texture somebody else authored
at an opacity; it has no colour, no tint, no blend parameter. Whatever the overlay is meant to
look like has to already be in the `.dds`.

**The census.** Across all 16 overlay packs installed on the development machine, 959 templates:

| | |
| --- | --- |
| biped slot 3 (body) | 925 |
| biped slot 4 (left hand) | 34 |
| **head** | **0** |
| matching `sweat` / `blush` / `perspir` | **0** |

No F4EE skin-override data is installed either -- `F4SE/Plugins/F4EE/` holds only `Overlays`,
`Presets` and `Sliders`. So there was nothing to drive and no way to recolour what exists, which
is why `tools/make_overlays.py` exists and why these are the only assets Rapport ships.

Two things follow that are worth writing down.

**`quantity` PICKS.** An `overlayGroup` with two templates in it applies *one of them*. A set that
must apply both a body texture and a face texture needs two groups of one, not one group of two.
`condition` is mandatory (`minOccurs="1"`); `overlayGroup` is unbounded.

**Slot 0 is unproven.** Since not one installed template targets the head, nothing here
demonstrates that F4EE applies a head overlay at all. Rapport's blush is written as an experiment
on that question, and the sweat does not depend on the answer.

### The BGEM, since one had to be written

A Bethesda effect-material is a 63-byte header, a length-prefixed NUL-terminated texture path, a
10-byte gap, a second path, and a 52-byte tail. **The length counts the NUL** -- a 29-character
path is written as 30.

The check that matters is not that the file parses. A first attempt rebuilt the header field by
field from a reading of the format and produced 65 bytes where the working file has 63; because a
BGEM is parsed positionally, those two bytes shift every field after them. It still passed a
"round-trip test" that scanned the result for plausible strings, which is a test that cannot fail.
The honest test is to rebuild a material the engine already loads and compare bytes -- see
`tools/make_overlays.py`, which does exactly that.
