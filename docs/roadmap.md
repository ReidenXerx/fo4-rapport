# Roadmap

Agreed 2026-09-18. Order is by dependency, not by appetite: each item needs the one above it.

## Done

| | |
| --- | --- |
| M0 | Scheduler, loaded-actor enumeration, timing counters. 0.006 ms average against a 0.25 ms budget. |
| M1 filters | Adults only (engine check verified in game, plus a race allow-list that fails closed), never a quest-driven actor, not in combat or dialogue. |
| M1 scoring | Proximity, shared faction, indoors, night, onlookers, player presence. Curves rather than lines. |
| M2 bridge | Papyrus bridge, generated ESP, native↔Papyrus doorbell. **A real scene: Geneva and a Diamond City guard, 2026-09-18.** |

## Now

**AAF events reaching the bridge.** Everything below waits on it: without them Rapport cannot tell
when a scene began, ended, or who was in it, and the watchdog is doing all the releasing.

## Next

1. **Co-save state** — need, cooldowns, last partner, refusal memory. Versioned, length-checked,
   unresolvable actors dropped on load. The foundation, and the same machinery aftermath needs.

2. **Aftermath** — overlays, morphs and expressions that outlive the scene, persisted in game time
   so they survive a reload and are never stranded. First thing a player actually notices. See A-10.

3. **The Attraction stat** — persistent, written through AAF so its own UI shows it and other mods
   can read it. See A-3.

4. **The addon API** — Papyrus functions and an F4SE message API. The moment it exists, the autonomy
   policy leaves Rapport's scheduler for Chemistry, where it belongs.

5. **M3 hardening** — `FindMatchingAnimations` as a pre-check so a pair with no content is never
   chosen, interruption handling, travel and privacy.

6. **More framework** — the owner has a queue of ideas here, and this is the right place for them:
   general sex mechanics that every future mod reuses. Rapport is the ecosystem's shared layer, so
   anything two mods would both want belongs here rather than in an addon.

7. **Player Proposals** — the Sex 'Em Up replacement, in its own repository.

   Known risk, flagged early: it needs **dialogue records**, and dialogue is a far heavier ESP
   structure than the single quest record `tools/make_esp.py` writes by hand. This is the one place
   the real Creation Kit would genuinely help, and it is worth solving before the design depends on
   it rather than after.
