# Safeguards — what happens when something goes wrong

Rapport puts state on other people's NPCs: a busy keyword AAF owns, an overlay that persists by
design, a facial expression that is locked against anything else moving it. Every one of those
outlives the thing that put it there. The rule this file exists to enforce:

> **Nothing Rapport applies may ever be something only Rapport can remove.**

## The four ways it can go wrong

### 1. A save is written in the middle of a scene

The most likely one, and the one that breaks NPCs permanently.

At that moment two actors are carrying `AAF_ActorBusy`, put there by AAF's own `StartScene`, and
**only a scene ending clears it**. The scene the save remembers does not exist after a reload — AAF's
DLL state does not persist, the plugin starts from nothing, and nobody is left who knows those two
were ever busy. They stay flagged for the rest of the playthrough, refused by *every* AAF mod on the
machine, with nothing anywhere saying why. That is how one NPC became permanently unusable during
development, and it took a session to work out.

Three things now cover it:

- **The save records the pair.** `SCNE`, two form ids, written on every save. A non-zero pair on load
  means the save was taken mid-scene; both are released and the log says so in as many words.
- **The bridge releases its own stale array.** `_inFlight` is a Papyrus variable, so it lives in the
  save while the plugin does not. `Connect()` used to clear it; now it releases every actor in it
  first. Clearing it without releasing was the same bug wearing a different hat.
- **The watchdog releases too.** A scene nobody reported is a scene we know nothing about, and not
  knowing what happened is exactly when state has to come off.

### 2. A face is left on somebody

Expressions are applied with `lock="true"`, which holds a morph against everything else that would
move it — including the face's own idle and dialogue animation. A locked face is a permanently
frozen face.

`FACE` in the save holds everyone currently wearing a Rapport expression. It is empty almost always
and non-empty exactly during a scene, which is when a save is most likely to strand one. On load
they are all cleared.

**Confirmed working 2026-09-18.** An NPC photographed after a scene has a normal, neutral face and
has gone back to what she was doing. `RemoveMFGBlock` does release a `lock="true"` morph, so the
frozen face this was built against does not happen. The belt-and-braces stays: it costs one call,
and nothing installed could have answered the question in advance.

Clearing is its own order kind rather than "apply the zeroed set", because **the zeroed set may not
be enough**. Every `mfgSet` in every installed pack sets `lock="true"` and not one ships
`lock="false"`, so nothing on this machine demonstrates what releases a lock. The clear therefore
applies `Rapport_Clear` *and* calls `RemoveMFGBlock` with all fifty morph ids. One extra call, no
guess.

### 3. An overlay never expires

The whole point of aftermath is that AAF's own timer cannot survive a save. Ours can — but a saved
expiry is still a float in a file, and a float can be nonsense. An expiry that is NaN compares false
against every `>=`, so the overlay would never expire: the exact outcome the feature exists to
prevent, arrived at from the other direction.

On load an expiry that is not finite is set to zero, which expires it immediately. Better a removal
one hour early than an overlay that is there forever.

### 4. The player wants out

`PanicClear = 1` in `Rapport.ini`. Load a save: every overlay Rapport applied comes off, every face
it set is cleared, the ledger is emptied, and the log says so and reminds you to set it back to 0.

It runs on the first tick after a load rather than at startup, because the ledger is empty until the
save has been read and you cannot take off what you do not know is on.

**Run it before uninstalling, not after.** Once the plugin is gone there is nothing left that knows
which overlays were Rapport's — they are ordinary LooksMenu overlays on those actors, and only the
player, by hand, in LooksMenu, can take them off.

## Two smaller ones

**The ledger is pruned.** `PruneHours`, 720 game hours by default. An actor whose last scene and last
refusal are both older than that, and who has no overlay standing, is dropped on save. A 200-hour
playthrough would otherwise carry a record for every settler who ever stood near anyone. Anyone still
wearing something keeps their record whatever its age — the overlay outlives the memory of how it got
there, and dropping the record would not remove the overlay.

**Refused actors are benched, not retried.** `BusyBackoffSeconds`, 300. An actor AAF refuses as busy
is passed over rather than offered again on the next tick. Johnny Friendly burned four consecutive
ticks before this existed.

## What is not covered

**Actors flagged busy by something that is not us.** The sweep is deliberately narrow: Rapport
releases actors *it* had hold of, and no others. A broader sweep would have to decide that a flag set
by another mod is stale, and AAF exposes no way to ask whether a scene is running — `GetAAFStatus`
is a readiness flag, not a count. Clearing another mod's reservation mid-scene would break it, so
the framework does not guess.

**An overlay applied before Rapport knew about it.** CumOverlays' own applications are not in our
ledger. Stopping its quests means no new ones; the ones already standing expire on AAF's timer or
stay, and `PanicClear` will not touch them because we never recorded them.

### 5. An AAF body morph is left on somebody (added 2026-09-23)

**It has happened.** Reported by the Silhouette session and measured from F4SE co-saves: the Diamond
City guard `000F61B6` (`DiamondCitySecurityMayorAlways`) held LooksMenu morphs `Erection = 1.0` and
`CErection = 1.0`, keyed to `AAF_MorphKeyword` (AAF.esm `000F9E`, confirmed by reading AAF.esm), in
two consecutive saves. He is one of the two actors in Rapport's first real scene (2026-09-18,
`roadmap.md`), so it is very likely ours; how the morphs survived is NOT established. AAF normally
removes them at scene end. It matters beyond the look of it: LooksMenu runs BodyGen only for an actor
with no stored morphs, so a leftover one keeps that actor out of every body-distribution mod.

**What Rapport does now** (`src/Morphs.h`, order kind 33, `Bridge.ClearAAFMorphs`): clears the
`AAF_MorphKeyword` layer, and only that layer, with `BodyGen.RemoveMorphsByKeyword` for both sexes and
`UpdateMorphs` when the actor's 3D is loaded. BodyGen's bodies and the player's sliders sit under
other keys and are untouched. It is not an AAF call, so it cannot strand the drain. It skips anyone AAF
has marked `AAF_ActorBusy` right now. It runs for:

- the pair of a scene that ENDED, **25 s later**: after the 20 s afterglow, so AAF's own teardown is not
  cleared under its feet;
- the pair of a scene the watchdog ABANDONED, and of a request that FAILED;
- the pair a save caught MID-SCENE (`SCNE`), which never reaches the ledger;
- on every load, **everyone in the ledger**, before AAF has announced itself.

**Verified 2026-09-23:** the load sweep queued, drained and traced both actors in the test save's
ledger. The removal call itself, issued on the actual guard through the console, left his entry with
**no morphs at all** in the next co-save (`f4mcp-morphtest`). **Not verified:** the scene-end,
abandon and mid-scene paths in a live scene.

**It cannot touch Rapport's own aftermath** (checked 2026-09-23, on the owner's question). Overlays go
through AAF's `ApplyOverlaySet` into LooksMenu's OVERLAY store, not its body morphs. Commonwealth
Moisturizer is worn meshes and headparts, and its four scripts' only form lookup is `AAF_API` itself
(`CMkz_EventHandlerScript.psc:181`), with no `BodyGen`, `SetMorph` or morph keyword anywhere. Faces are MFG
morphs, another system again. **The coupling to remember:** if aftermath ever adopts AAF's
`ApplyMorphSet` (roadmap, Next 2), those morphs would presumably be stored under this same
`AAF_MorphKeyword`, and the 25 s clear would take them off. That feature must change this one first --
clear only AAF's scene-state morphs by name, or skip actors carrying standing morph aftermath.

**Rapport never regenerates a body** (owner, 2026-09-23). If AAF's layer was all an actor had, the
clear leaves an EMPTY entry, and LooksMenu keeps BodyGen away from an actor with any entry -- but only
until the next save and load: it never loads an empty one (the Silhouette session read it in
`BodyMorphInterface.cpp`), so from then on BodyGen gives them their body by itself. Asking
`RegenerateMorphs` to do it at once was considered and REJECTED: re-rolling a body is not something the
player asked this mod for.

**The sweep's cost.** One order per ledger actor, on EVERY load, and the drain takes eight a poll. A
ledger of 200 is about 75 s of queue in front of anything queued after it — overlays re-applied after
the load, for instance. Cosmetic delay, not breakage; if ledgers grow that large, the sweep wants to
become one order that loops on its own stack.

**Two limits worth knowing.** That save's ledger does NOT contain the guard: his scene is outside its
history, so the sweep never reaches him. Only an actor Rapport has a record of is swept, which is the
same rule as every other safeguard here. His entry also stays behind EMPTY; whether LooksMenu then
runs BodyGen for him is LooksMenu's decision (Silhouette's "new bodies" button regenerates him either
way). The check, in any save and without the game: `fo4-silhouette/tools/cosave_census.py "<save>.f4se"`.
