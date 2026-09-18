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

## Pulled forward

**The debug hub** (was item 6). One profile in `debug.json` drives diagnostics across the whole
stack, because debugging this mod means debugging three at once and each keeps its settings
somewhere different:

| Tier | What | When it applies |
| --- | --- | --- |
| Live | AAF settings via `ChangeSetting`, any MCM mod's settings via `MCM.SetModSetting*` | Immediately |
| Next launch | `bEnableLogging` / `bEnableTrace` in `Fallout4Custom.ini`, backed up once | The engine reads them at startup, and Rapport says so |

Every change is named in `Rapport.log`. The point is not switching diagnostics on, it is switching
them reliably back off — a stray `troubleshooting_level` put six modal pop-ups in front of the owner
and nothing but memory would have turned it off again.

## Next

1. **Co-save state** — need, cooldowns, last partner, refusal memory. Versioned, length-checked,
   unresolvable actors dropped on load. The foundation, and the same machinery aftermath needs.

2. **Aftermath** — overlays, morphs and expressions that outlive the scene, persisted in game time
   so they survive a reload and are never stranded. First thing a player actually notices. See A-10.

3. **Takeover** — Rapport configures the mods it works alongside so the player never has to:
   stopping CumOverlays while our aftermath runs, disabling Autonomy Enhanced and Sex 'Em Up, and
   any AAF setting we depend on. Automatic, but never secret: detected before acting, previous
   values recorded, listed in the UI with reasons, every change named in the log. See A-11.

   It sits here rather than earlier because restoring a setting exactly means remembering what it
   was, and that has to outlive the session — so it needs the co-save above it.

4. **The Attraction stat** — persistent, written through AAF so its own UI shows it and other mods
   can read it. See A-3.

5. **The addon API** — Papyrus functions and an F4SE message API. The moment it exists, the autonomy
   policy leaves Rapport's scheduler for Chemistry, where it belongs.

6. **M3 hardening** — `FindMatchingAnimations` as a pre-check so a pair with no content is never
   chosen, interruption handling, travel and privacy.

7. **More framework** — the owner has a queue of ideas here, and this is the right place for them.
   The debug hub was the first of them and has already been pulled forward. Remaining:
   general sex mechanics that every future mod reuses. Rapport is the ecosystem's shared layer, so
   anything two mods would both want belongs here rather than in an addon.

8. **Player Proposals** — the Sex 'Em Up replacement, in its own repository.

   Known risk, flagged early: it needs **dialogue records**, and dialogue is a far heavier ESP
   structure than the single quest record `tools/make_esp.py` writes by hand. This is the one place
   the real Creation Kit would genuinely help, and it is worth solving before the design depends on
   it rather than after.
