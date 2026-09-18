# Roadmap

Agreed 2026-09-18. Order is by dependency, not by appetite: each item needs the one above it.

## Done

| | |
| --- | --- |
| M0 | Scheduler, loaded-actor enumeration, timing counters. 0.006 ms average against a 0.25 ms budget. |
| M1 filters | Adults only (engine check verified in game, plus a race allow-list that fails closed), never a quest-driven actor, not in combat or dialogue. |
| M1 scoring | Proximity, shared faction, indoors, night, onlookers, player presence. Curves rather than lines. |
| M2 bridge | Papyrus bridge, generated ESP, native↔Papyrus doorbell. **A real scene: Geneva and a Diamond City guard, 2026-09-18.** |

| M2 events | AAF's seven events reach the bridge, matched to our request by AAF's own scene id. Argument layouts captured in `docs/aaf-api.md`. |
| 1 co-save | `Ledger` — what happened to each actor, in the save. Versioned, length-checked, unresolvable form ids dropped. See A-12. |
| 2 aftermath | `Aftermath` — overlays that outlive the scene, expiring in game hours from the save. See `docs/aftermath.md` and A-13. |
| 3 takeover | `Takeover` — CumOverlays' two quests stopped while Rapport owns aftermath, started again when it does not. See A-11. |

## Now

**Watching items 1–3 behave in a real game.** Built and deployed, not yet proven: the ledger has
never been read back out of a save, no overlay has been applied by Rapport, and no quest of another
mod has been stopped by it. Each one logs enough to be decisive on a single run.

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

1. **Aftermath, the parts tags cannot answer** — roles (who gave and who received; AAF knows, we do
   not ask yet), morphs, and expressions. The stony face the owner saw is here: `ApplyMFGSet` with
   SAM installed. Cheap now that the plumbing exists.

2. **Takeover, the rest of it** — Autonomy Enhanced and Sex 'Em Up switched off the same way
   CumOverlays already is, and any AAF setting we depend on. The mechanism is built and proven on
   two quests; this is a list, not a feature.

3. **The Attraction stat** — persistent, written through AAF so its own UI shows it and other mods
   can read it. See A-3.

4. **The addon API** — Papyrus functions and an F4SE message API. The moment it exists, the autonomy
   policy leaves Rapport's scheduler for Chemistry, where it belongs.

5. **M3 hardening** — `FindMatchingAnimations` as a pre-check so a pair with no content is never
   chosen, interruption handling, travel and privacy.

6. **More framework** — the owner has a queue of ideas here, and this is the right place for them.
   The debug hub was the first of them and has already been pulled forward. Remaining:
   general sex mechanics that every future mod reuses. Rapport is the ecosystem's shared layer, so
   anything two mods would both want belongs here rather than in an addon.

7. **Player Proposals** — the Sex 'Em Up replacement, in its own repository.

   Known risk, flagged early: it needs **dialogue records**, and dialogue is a far heavier ESP
   structure than the single quest record `tools/make_esp.py` writes by hand. This is the one place
   the real Creation Kit would genuinely help, and it is worth solving before the design depends on
   it rather than after.
