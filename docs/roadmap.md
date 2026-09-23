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
   **Coupled to a safeguard:** the AAF-morph clear (`docs/safeguards.md` §5) removes everything under
   `AAF_MorphKeyword` 25 s after a scene, so morph aftermath must change that clear first.

3. **The Attraction stat** - superseded by the relationship module, which ships WITH the release (R-13) — persistent, written through AAF so its own UI shows it and other mods
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

9. **Use the fingerprints to find the ROSTER'S GAPS** (owner idea, 2026-09-21). The voice
   embeddings (V-25) already measure how close every unique voice - vanilla, and with item 8 every
   MODDED one - is to the 32 voices we render. Turned around, the same numbers say which kinds of
   voice we are MISSING: cluster the unique voices that sit far from all 32 (low best-match score,
   many NPCs using them) and each dense far-away cluster is a voice archetype worth rendering next.
   That turns "which voice types should we add?" from taste into a measured list, ranked by how many
   characters each new voice would cover. The owner still picks by ear (V-9); the list only says
   where to listen. Ties naturally to item 8 (mod voices make the gaps real on a given install).

10. **Companion module in Overture** (owner idea, 2026-09-21; `relationship-and-personas.md` N-7).
    Sex with companions is its own module in the next mod, with a standalone relationship system
    bound to the same relationship store and carrying companion-only modifiers. Until then
    companions stay out of autonomy: they are deliberately not on `ambientQuests`.

11. **Narrator** (owner idea + poll, 2026-09-21). An optional Rapport module that says, in a line,
    what is about to happen and WHY, so a player can see the machinery if they want to. Because
    every mod on Rapport goes through it (Chemistry, Overture, Player Proposals), one module
    narrates all of them. Settled by poll:
    - **Surface:** a HUD notification, plus a readable history of recent entries (an MCM page or a
      holotape, to decide when built).
    - **Voice:** a wry one-sentence storyteller line carrying the real reasons (place, time, crowd,
      bond, persona), then a second line with the actual score parts, e.g.
      `Marcy and Sturges slip away - late, quiet, and they get along.`
      `[score 1.20 = near +1.4, night +.25, 5 watching -.9]`
    - **Moments:** scene starts, why nothing happened (a likely pair passed over), relationship
      turns (a bond crossing a threshold, a first time), bystanders reacting. **Each is an MCM
      checkbox**, so players choose.
    - **Scope:** every scene in the loaded area, not only ones the player could notice.
    Needs from the core: the score breakdown per pair (Rapport has the parts in `PairSignals`; an
    addon's own bonuses must be reportable too, so Chemistry can say "+0.45 bond"), and an event
    for each moment. Not designed further yet.

12. **What Overture's methodology asks of the core** (proposed 2026-09-23, overnight, nothing built;
    `fo4-overture/docs/methodology.md`). Each is here because a third mod would want it too (O-1):
    - `RecordExternalScene(a, b)` - a scene that was not Rapport's still happened. Ivy's own "Favor: Sex"
      is a fade to black; without this her scenes raise no bond and count as nothing. Bond +15% of the
      distance left, the pair's scene count, `HoursSincePair` - everything `RecordScene` does, minus AAF.
    - `PlaceOf(npc)` - nobody's / theirs / their faction's / the player's. Chemistry computes it in
      Papyrus (`WhosePlace`); Overture needs it once per conversation.
    - `StrongestBondOf(npc, excluding)` - the other person and the value, so a couple Chemistry has made
      (0.75, "inseparable") can be spoken for in Overture the way an engine spouse is.
    - **APPROVED by the owner 2026-09-23 (Overture O-15, behind an MCM switch).** **A store-side partner flag an addon can set** (player-and-NPC lovers, set by Overture), which
      `Rapport:Relations.HasPartner` would then see - so Chemistry's faithfulness charge applies to the
      player's lover too, and a scene anyway is an affair against the player. This one CHANGES what
      Chemistry does, which is why it is an owner poll in the methodology, not a plan.
    - **Skip barks for the player.** `Barks::OnSceneStarted` picks a line for BOTH participants by persona
      and never checks for the player, so in a scene Overture requests the player would speak an NPC bark
      in a persona hashed from `0x14` -- against R-11 (the player has no persona) and Overture's O-2/O-3
      (the player's side is text). Must land before any player scene is switched on (review, 2026-09-23).
    - **APPROVED by the owner 2026-09-23 (Overture O-16).** **A priority lane for a deliberate player request.** Rapport runs one scene at a time, and Chemistry
      can take the slot in the seconds between an NPC's yes and the request. A reservation, or player
      requests outranking autonomy, is an owner poll in Overture's methodology.

13. **Dead AAF positions: prefer the UAP duplicate** (found by the fo4-mcp session, 2026-09-23; memory
    `aaf-uap-original-positions-dead`). On the owner's install, a position named from a pack's ORIGINAL
    XML ("BP70 Missionary", "Atomic Cowgirl") fires OnSceneInit and OnAnimationStart, applies morphs and
    strips, and then both actors stand in vanilla idles for the whole scene. Only "[UAP] ..." positions
    animate: UAP replaced the pack plugins, and the leftover original XMLs point at idle forms that no
    longer exist. AAF logs nothing about it. So OnAnimationStart is not evidence that anything plays,
    and a tag match can hand Rapport a dead position. When a position has a UAP duplicate (its tags
    include "UAP"), pick the duplicate. Longer term, check a frame. Not built.
