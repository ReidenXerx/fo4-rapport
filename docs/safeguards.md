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

**An AAF body morph left on somebody — NOT COVERED, and it has happened.** Reported 2026-09-23 by
the Silhouette session, measured from F4SE co-saves rather than inferred: the Diamond City guard
`000F61B6` (`DiamondCitySecurityMayorAlways`) holds LooksMenu morphs `Erection = 1.0` and
`CErection = 1.0`, keyed to `AAF_MorphKeyword` (`KYWD 000F9E` in AAF.esm), in two consecutive
co-saves (2026-09-22 21:39 and 23:05). Nothing else in the save holds an AAF morph. **He is one of
the two actors in Rapport's first real scene** (Geneva and a Diamond City guard, 2026-09-18 — the
ledger line in `roadmap.md` names `000F61B6`), so this is very likely ours. The cause is NOT
established: AAF normally clears its keyed morphs at scene end, and this pair survived, maybe through
an early `StopScene`, an interrupted scene or a load.

Why it matters beyond the look of it: LooksMenu runs BodyGen only for an actor with NO stored morphs,
so a leftover morph keeps that actor out of every BodyGen distribution (Silhouette's included) for
the rest of the save.

The fix, specified by the reporter and still to be checked against LooksMenu's own `BodyGen.psc`
before a line is written: wherever Rapport knows a scene ended, was cancelled or was left behind by a
load, for each actor it put in the scene call `BodyGen.RemoveMorphsByKeyword(actor, isFemale,
AAF_MorphKeyword)` and then `BodyGen.UpdateMorphs(actor)`, from the Papyrus side (the bridge drain;
C++ never calls the VM). This clears ONLY AAF's keyed layer, never BodyGen's or the player's sliders.
Plus a load-time sweep of the actors in `SCNE`. The check, in any save, without the game:
`fo4-silhouette/tools/cosave_census.py "<save>.f4se"` lists every actor holding body morphs.
