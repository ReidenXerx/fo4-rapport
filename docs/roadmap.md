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

| 1b expressions | `Expressions` — nine of Rapport's own facial sets, driven as fractions of a scene. No textures, no dependency. See A-14. |
| 1c safeguards | `SCNE` / `FACE` records, stale-array release, NaN expiry, pruning, `PanicClear`. See `docs/safeguards.md` and A-15. |
| 3b takeover | Three mods now: CumOverlays, Sex 'Em Up, Autonomy Enhanced. Each owned by a feature. See A-16. |

| **M4 the whole chain** | **2026-09-18 21:48. Scan, score, scene, expressions, stop, scene end, ledger, aftermath, apply — end to end, unattended, and then straight into the next scene.** |

| 4 scenarios | `Scenarios` — sex as a story. A named pipeline of stages, each a narrative ROLE with a duration and acceptable tags rather than a position. Rapport executes one; only an addon chooses it. See A-24. |
| 4b moisturizer | Commonwealth Moisturizer as an aftermath backend — worn geometry and morphing headparts rather than texture overlays, driven through its own API from an optional second plugin so no Rapport script names a type that mod owns. Roles and layering: the receiving actor, in the hole last touched. |
| 5 aaf watchdog | `AAFHealth` — AAF stops answering after a load and nothing says so. Restarted through its own `EveryTime_Initialization`; a stopped main quest asks the player first. |

## Now

**It runs.** One scene, start to finish, everything downstream of it working:

```
expressions: 5% through - Rapport_Anticipation ... 95% - Rapport_Climax   (5 steps, both actors)
bridge: request 1 has run its length - asking AAF to stop it
aaf: OnSceneEnd                                    <- StopScene DOES produce it
request 1: scene ended
ledger: 00002F0B and 000F61B6 have now had 1 and 1 scene(s), at hour 780.0
aftermath: keep Moisturizer [OF] until hour 792.0
moisturizer: applied front=True oral=True rear=False
expressions: clearing 2 face(s) - the afterglow is over
2 candidate(s) within the 24-hour cooldown        <- the ledger is READ, not just written
```

The tag routing earned its design in that one scene: it began on `PenisToVagina` and drifted into an
Atomic Lust blowjob tagged `PenisToMouth`, and aftermath accumulated both across the animations and
resolved `[OF]` — oral and front. That is the behaviour the accumulate-then-decide-at-scene-end
shape exists for, arrived at by accident rather than by test.

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

1. **Roles** — who gave and who received. Tags say WHAT an animation was, never to whom, so both
   actors currently get the same overlay sets and the same face. AAF knows: `GetActorData` and the
   position's own role list. This is the last thing standing between aftermath and being right
   rather than merely working.

2. **Body morphs** — `ApplyMorphSet`, the third of AAF's three appearance levers and the only one
   Rapport does not touch. Unlike overlays it needs no textures; unlike expressions it is not
   instant, so it wants its own persistence.

3. **The Attraction stat** — persistent, written through AAF so its own UI shows it and other mods
   can read it. See A-3.

4. **The addon API** — Papyrus functions and an F4SE message API. The moment it exists, the autonomy
   policy leaves Rapport's scheduler for Chemistry, where it belongs.

5. **M3 hardening** — interruption handling, travel and privacy.

   **Not** `FindMatchingAnimations` as a pre-check. That was the plan and it is rejected: it is
   asynchronous, its argument layout has never been observed in this project, and a stage that
   stalls waiting for an answer is worse than one quietly dropped. The deeper reason is that the
   question it would answer is the wrong one — a tag index knows a tag exists SOMEWHERE, never
   whether an animation exists for THIS pair in THIS place. AAF's own refusal is the only real
   evidence, which is why a stage offers alternatives and they are tried one at a time until one
   is accepted. Do not rebuild this without new evidence that addresses those two reasons.

6. **More framework** — the owner has a queue of ideas here, and this is the right place for them.
   The debug hub was the first of them and has already been pulled forward. Remaining:
   general sex mechanics that every future mod reuses. Rapport is the ecosystem's shared layer, so
   anything two mods would both want belongs here rather than in an addon.

7. **Player Proposals** — the Sex 'Em Up replacement, in its own repository.

   Known risk, flagged early: it needs **dialogue records**, and dialogue is a far heavier ESP
   structure than the single quest record `tools/make_esp.py` writes by hand. This is the one place
   the real Creation Kit would genuinely help, and it is worth solving before the design depends on
   it rather than after.

   **Update 2026-09-21: solved.** `tools/make_dialogue.py` now generates quest-owned Topics and
   voiced lines (QUST > DLBR + DIAL > INFO), spoken in game by `Say` - see V-23. The risk above no
   longer stands; the lesson that does is that a Topic without a Dialogue Branch resolves and stays
   silent.

8. **Voice fallback for MOD-added voices** (owner: not a priority, 2026-09-21). The Voices service
   (V-25) measures the official masters' voice types only, so a voice added by a mod - 3DNPC alone
   brings hundreds - gets the plain sex default (and only for a human or ghoul actor). Extending
   `scripts/voice-inventory.py` and `voice-races.py` to the player's ACTIVE plugins and their voice
   archives / loose `Sound/Voice/<plugin>/` folders would give them measured matches too.
   `rebuild-voices.py` is already incremental, so it is the inputs that grow, not the pipeline.
   Mind that the map becomes per-install then, and the shipped table cannot contain it.
