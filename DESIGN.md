# Autonomy Framework for Fallout 4: design document

*Working title. v0.2, 2026-09-17. Target: Fallout 4 1.10.163 (old-gen) + F4SE 0.6.23 + AAF.*

## 1. Goal

One mod with two modules on a shared core, both plausible in the world and nearly free in
performance, even on a Steam Deck running an 800-plugin load order:

- **NPC Autonomy:** adult NPCs occasionally start AAF intimate scenes with each other.
- **Player Proposals:** the player can flirt with and propose to NPCs through natural dialogue, and the NPC decides.
  This replaces Sex 'Em Up.

**Design principles**
1. **Performance first.** Heavy logic runs natively. Papyrus only starts scenes.
2. **Believability over frequency.** Fewer scenes in the right place beat many in the wrong one.
3. **Save-safe.** Versioned state, no persistent references, clean uninstall.
4. **Everything configurable, nothing that relies on hotkeys.** Every critical control lives in MCM.

**Hard rules (not configurable)**
- Adults only. Children are excluded by the engine child check (race "Child" flag / IsChild) *and* an explicit blocklist.
- Never involves actors in combat, in an active quest scene, dead, disabled or unloaded.
  Autonomy also skips actors in dialogue.
- **Consent is the mechanic.** NPCs can say no, and a no is respected. There are no charm/calm spells, drugs or other coercion.
- The player takes part only by their own choice (Player Proposals) or through NPC invitations they opted into.

**Non-goals (v1):** animation authoring (use existing AAF packs), non-consensual content.

---

## 2. Why existing mods fall short (measured 2026-09-17 on the Deck)

| Finding | Evidence |
|---|---|
| **A per-NPC spell running a script is the main performance problem.** Random Overlay Framework gives every loaded NPC a magic effect whose script checks the NPC against a long faction list with `IsInFaction`, one native call per faction. | ROF was present in **62–80%** of the stuck Papyrus stacks in each of 4 overload events (largest: 1,094 stacks). |
| **Congestion comes in bursts right after loading a save or entering a town.** | All 4 overload events happened in the first 6 minutes after a load, then none for 35 minutes. |
| **Rechecking everyone on a timer wastes work** even when nothing will change. | ROF's repeat manager ran the faction scan although every repeat option was "Once". |
| **Error spam costs little; congestion causes latency.** Papyrus runs within a per-frame time budget, so overload delays *all* mods' scripts instead of dropping frames. | 2–43 errors/min from other mods had no measurable cost. |
| **Autonomy Enhanced's core is cheap,** but its controls depended on scripts that weren't shipped. | AE appeared in 3 of 1,752 stuck stacks, and its 3 hotkey quests had missing scripts. |
| **Saved-data bugs can hang loading.** Mod Switch Framework 1.3.0 wrote a field its loader didn't read and looped billions of times. | Save49 hung on load 2 out of 2 times. |
| **Sex 'Em Up (AAF_SEU V1.19) relies on menus and proximity spells.** Message-box flirt menus sit on top of a dialogue quest, an area scan and "calm" cloak spells find targets, and a generic follow package moves them. | Record/script names in `AAF_SEU.esp`: `SEUFlirtSimpleMenu`, `SEUActiveAreaScan`, `SEUCalmCloak`, `SEUFlirtFollowPkg`. It barely appeared in the log (1 stuck stack) because it mostly runs on use. The problem is immersion and a design that scales with nearby NPCs, not constant load. |

---

## 3. Architecture

```
 ┌──────────────── F4SE plugin (C++) ────────────────┐        ┌──── Papyrus bridge (1 quest) ────┐
 │ Autonomy scheduler (tick 10–30 s, time-sliced)     │        │  StartScene(actors, spot)         │
 │  → loaded NPCs near player → filters → scoring     │ event  │   → AAF API: start scene          │
 │                                                    ├───────►│   → travel package to the spot    │
 │ Proposal evaluator (called once per dialogue       │        │  AAF scene start/end events       │
 │  choice): chance, answer, spot suggestion          │◄───────┤   → report back (cooldowns, state)│
 │                                                    │ result └───────────────────────────────────┘
 │ Shared: eligibility rules, spot finder, relation-  │        ┌──── Dialogue quest (proposals) ───┐
 │  ship/interest memory, cooldowns → co-save         │◄───────┤  engine-evaluated conditions;     │
 │ Config: INI + JSON data tables, MCM globals        │ native │  a script runs only when the      │
 └────────────────────────────────────────────────────┘  call  │  player picks a line              │
                                                                └───────────────────────────────────┘
```

### 3.1 Native core (F4SE plugin)
- **Library:** a CommonLibF4 variant that supports 1.10.163 through the Address Library (`version-1-10-163-0.bin`).
- **Scheduler:** wakes every *N* seconds (default 20). Evaluation is spread across frames with a budget
  (default **0.25 ms per frame**). It does nothing while menus are open, during loading, or while the player is in combat.
- **Warm-up after load:** the first tick happens 60 s after a save loads. This avoids the post-load burst measured above.
- **Actor source:** the engine's list of loaded, high-detail actors, limited to a radius around the player (default 4096 units).
  No cloak spells and no per-NPC scripts.
- **Filters, cheapest first:** exclude children, then dead/disabled/deleted actors, then actors in combat, dialogue or a quest scene,
  or already in a scene. Then apply the blocklists (keywords, factions, form lists from config), the follower/companion policy, and
  essential/protected actors reserved by active quests.
- **Scoring for a candidate pair:**
  - relationship rank and a stored "partner memory"
  - shared settlement or faction (compatibility table from JSON, not script loops)
  - location type (interior, player home, settlement bed, bar) and privacy (nobody else within *R*, no line of sight to the player unless "brazen" is on)
  - time of day and a slowly rising per-actor *need* value
  - per-actor and per-location cooldowns, plus a daily cap per location
- **Output:** at most one scene request per tick, and at most `MaxConcurrentScenes` (default 1) near the player.

### 3.2 Papyrus bridge
- A single quest script that receives the request from native code (a dispatched Papyrus call).
- **Travel phase:** attach both actors to aliases with a travel/sandbox package toward the chosen spot (bed or furniture).
  Timeout 90 s: if they don't arrive, abort and restore.
- **Scene:** call AAF's Papyrus API to start the scene with those actors at that spot. Check the exact function and
  event signatures against the installed AAF API script; don't copy them from memory.
- **Scene start/end events:** report back to native code, which then sets cooldowns, updates memory and clears aliases.
- **Interruptions** (combat starts, player enters, cell unloads, save/load during a scene): end the scene, clear packages and aliases.
  A watchdog force-ends any scene still running after *N* minutes.

### 3.3 Persistence (F4SE co-save)
- A record per actor: `formID, need, cooldownUntil (game time), lastPartner, flags`.
- **Lesson from MSF 1.3.0:** write and read go through **one** shared, versioned serializer.
  - Every field read is checked against the record's remaining length.
  - Counts are sanity-capped.
  - Unknown versions are skipped, not guessed.
  - A unit test round-trips every version.
- On load, drop state for actors that no longer resolve.

### 3.4 Configuration
- `Data/F4SE/Plugins/AutonomyFramework.ini`: performance knobs (tick, budget, radius, warm-up, max concurrent scenes).
- `Data/F4SE/Plugins/AutonomyFramework/*.json`: faction compatibility, location weights, blocklists. Read once at startup.
- **MCM:** all switches are global values or mod settings, so the menu works with no helper scripts.
  Hotkeys are only shortcuts, never the only way to do something.

---

## 4. Performance budget and acceptance rules

| Metric | Budget |
|---|---|
| Native evaluation | ≤ 0.25 ms/frame average, ≤ 2 ms worst single frame |
| Papyrus | 1 bridge quest; no per-NPC scripts; no polling loops |
| Scene requests | ≤ 1 per tick; ≤ `MaxConcurrentScenes` near the player |
| After loading a save | Zero work for 60 s |
| Script overload events | **0** added compared with a baseline session without the mod |

**How to measure** (tools already on the Deck: `~/fo4-loadwatch.py`, `~/fo4-hangcap.py`):
- Papyrus log: count "suspended stacks over threshold" events and dumped stacks, same save and route with the mod on vs off.
- Native timing counters logged once a minute (average/max ms per tick, actors evaluated, requests sent).
- Frame times with MangoHud; save load time for the same save.

---

## 5. Immersion rules

**v1**
- **Who:** adults with relationship rank ≥ configured value, *or* living in the same settlement with enough need.
- **Where:** interiors, owned or assigned beds, player homes (optional). Public spaces only with "brazen" enabled.
- **When:** night weighting, not during alarms or combat nearby, a daily cap per location.
- **How:** walk to the spot first, then the scene, then a short idle afterwards. Everything ends cleanly on interruption.

**v2 ideas**
- Witness reactions (a comment or idle from nearby NPCs).
- Partner memory influences future choices (steady couples vs one-offs).
- Settlement mood/happiness hooks.
- Per-NPC opt-out via a console command or item.

---

## 5b. Player Proposals: flirt, propose, decide (replaces Sex 'Em Up)

**1. Dialogue, not menus.**
- A dedicated dialogue quest adds a few player lines to eligible NPCs:
  - **Flirt:** low stakes; raises or tests interest.
  - **Propose:** appears once there is some interest, or as a check.
  - **Let's go somewhere private:** follow-up.
- The engine evaluates the line conditions only while you're talking to that NPC, so nothing runs in the background.
- The odds show with FO4's own persuasion colors (green / yellow / red), like vanilla Charisma checks.
- **Voice:** player lines are silent. NPC answers show as subtitles by default. Optionally, reuse matching vanilla
  voice-type lines ("Sure." / "Not interested.") plus a nod or head-shake idle. Voice is FO4's hardest immersion limit, so scope it honestly.

**2. The NPC decides (native, one call per choice).**
- **Inputs:**
  - Charisma and perks (Lady Killer / Black Widow)
  - the NPC's **interest** in the player (built through conversations, gifts and quests done for their faction)
  - disposition from a data table: open / reserved / committed
  - current need (shared with Autonomy)
  - privacy of the location, time of day, recent refusals
- **Answers:**
  - **Yes**
  - **Not now:** soft refusal plus a cooldown
  - **No:** interest drops and the option hides for a while
- Asking again after a refusal lowers the odds. It never raises them, so there's no harassment loop.

**3. Going somewhere private.** The NPC proposes a spot (their bed, a room nearby, your player home)
and walks there with you: a travel package targeting the player, with a timeout. "Right here" is only allowed if the spot passes
the privacy check. It uses the same spot finder as Autonomy.

**4. Scene** through the same Papyrus bridge and AAF, with the same interruption handling and watchdog.

**5. Afterwards.** Interest and partner memory go into the co-save. Greetings reflect the relationship next time.
Companions use their own affinity (opt-in per companion).

**6. v2 ideas:** NPCs with high interest occasionally invite *you* (rare, private places only, opt-in); partners react;
reputation consequences if someone sees.

**Performance:** zero background cost. There are no area scans or cloak spells. Code runs only when you pick a dialogue line or a scene starts.

---

## 6. Safety and compatibility
- **Dialogue compatibility:** the proposal quest uses its own low-priority topics with strict conditions: never during quest
  dialogue, never on NPCs in active quest scenes, never in combat. Test it against dialogue-heavy mods before release.
- **Quest safety:** skip actors in active quest scenes or aliases that reserve them. Companions are opt-in per companion.
- **AI mods** (settlers' work packages, Sim Settlements, Workshop Framework): use travel packages only temporarily and always restore them.
- **Uninstall:** an MCM button clears aliases, ends scenes and stops the scheduler, after which the plugin can be removed.
- **Nexus:** flag as adult content; the rules in section 1 are stated on the page.

---

## 7. Milestones (each one gated by the performance test in section 4)

| # | Deliverable | Done when |
|---|---|---|
| M0 | Plugin skeleton: logging, scheduler, actor enumeration, timing counters | Budget met with filters off; 0 added overload events |
| M1 | Filters + scoring in **dry-run mode** (logs chosen pairs, no scenes) | Chosen pairs look plausible across 3 test locations |
| M2 | Papyrus bridge + AAF scene start/end + cooldowns + co-save | 10 scenes without a stuck actor; save/load mid-scene is clean |
| M3 | Travel-to-spot, privacy check, interruption handling, watchdog | Combat, cell change and reload during travel or scene all recover |
| P1 | Proposal dialogue quest + native evaluator + persuasion colors, **dry-run** (shows the odds, never starts a scene) | Lines appear only on eligible NPCs; odds feel fair across 10 NPC types |
| P2 | Answers (yes / not now / no), interest memory, cooldowns, subtitles + reaction idles | A no is respected; asking again never raises the odds |
| P3 | Walk-to-spot with the player, scene through the bridge, greetings afterwards | Works in a settlement, a city interior and a player home |
| M4 | MCM (both modules), JSON config, uninstall, documentation | Fresh install and clean uninstall both verified |
| M5 | v2 extras: witnesses, partner reactions, NPC-initiated invitations (opt-in) | Still within the section 4 budget |

**Order:** M0 → M1 → M2 → M3 (shared core) → P1 → P2 → P3 → M4 → M5. Remove Sex 'Em Up once P3 is complete.

---

## 8. Tech stack and workflow
- **Native:** C++20, a CommonLibF4 variant supporting 1.10.163, built with Visual Studio 2022 on the PC.
  A clang-cl cross-build on Linux is possible later.
- **Papyrus:** Creation Kit compiler or Caprica; AAF API scripts as imports.
- **Testing:** PC (Windows) and Steam Deck (Proton). Same save, same route, mod on vs off. Logs are analysed with the tools from the 2026-09-17 audit.
- **Source control:** a git repository; this document is `DESIGN.md` at its root.

## 9. Risks and open questions
- How much of the scene start cost comes from AAF itself (XML/animation lookup)? Measure it in M2.
- Getting actors to reach a spot reliably without fighting other mods' AI packages (M3 is the riskiest milestone).
- Which CommonLibF4 variant to base on for OG 1.10.163 (check which ones current OG plugins use).
- Whether the AAF API behaves the same under Proton as on Windows (test in M2).
- NPC voices: FO4 dialogue is voiced, so subtitles-only answers may feel flat. Decide in P2 whether reusing vanilla lines per voice type is worth the effort.
- Adding player lines to generic NPCs without disturbing vanilla or quest dialogue (strict conditions, low-priority topics, test with dialogue overhauls).

---

# Amendments

Decisions taken after v0.2 of this document, each one the owner's. The sections above
are unchanged; where they disagree with an amendment, the amendment wins.

## A-1 (2026-09-17) — Three mods, not one

The document describes one mod with two modules on a shared core. It ships instead as a
**pure framework plus two addons**, so the proposals side can iterate and release without
re-releasing the autonomy core.

| | |
| --- | --- |
| **Autonomy Framework** | Plumbing only. Scheduler, actor enumeration and filtering, AAF bridge and actor locking, spot finding and privacy, attraction/need/cooldown/refusal state, co-save. Ships a public API (Papyrus functions and an F4SE message API) and no behaviour of its own. |
| **NPC Autonomy** (addon) | Decides when and whom. Requires the framework. |
| **Player Proposals** (addon) | Replaces Sex 'Em Up. Dialogue, the player's own choices. Requires the framework. |

The framework owns everything both addons would otherwise duplicate, so there is one scan,
one actor lock, and one saved state. Two independent mods were rejected: two schedulers
would scan the same actors, two co-saves would hold contradictory cooldowns, and both could
select the same NPC with only AAF's `SetActorLocked` preventing a collision.

The cost, accepted knowingly: the public API has to exist before either addon works, and a
careless framework change can break an addon.

## A-2 (2026-09-17) — Replace, don't coexist

AAF Autonomy Enhanced and AAF Sex 'Em Up are both removed when ours ships. Autonomy Enhanced
distributes a per-NPC perk on a timer, which is the pattern this design exists to eliminate.

## A-3 (2026-09-17) — We define the attraction stat

AAF's relationship layer is empty: no `relationshipStat` is defined anywhere, and of 12,120
compiled scripts in the reference load order nothing but AAF itself calls the stat API (see
`docs/aaf-api.md`). The framework therefore ships its own `actorStatData` declaring a
persistent Attraction relationship stat and writes it through AAF's API, so AAF's own UI
shows it and other mods can read it. The F4SE co-save remains authoritative for need,
cooldowns, last partner and refusal memory.

## A-4 (2026-09-17) — Who is eligible

- **Races:** Human, Ghoul, SynthGen1, SynthGen2, SynthGen2Valentine, DLC03_SynthGen2DiMa.
  Not supermutants, not creatures. These are exactly the six human-skeleton races AAF can
  animate on the reference setup; the list is an allow-list, so an unlisted race is never a
  candidate and a missing entry means "no scene" rather than "a scene with the wrong actor".
- **NPCs:** generic and named alike, but never one a quest is actively directing. Membership
  of a quest alias is not the test — settlers sit in settlement aliases permanently — an
  alias with *instanced packages* is.

## A-5 (2026-09-17) — Private by default, public if bold

Privacy is preferred and scored rather than required. A pair with high enough attraction, or
in a place that permits it (a raider camp, a bar), may go ahead in the open. The spot finder
stays load-bearing for the common case.

## A-6 (2026-09-17) — Non-hostile, factions weighted

A pair must be non-hostile to each other by the engine's own check. Shared faction, shared
location and time together raise the score, so same-faction pairs are far likelier while a
cross-faction pairing remains possible when everything else is favourable.

## A-7 (2026-09-17) — CommonLibF4 variant settled

The document's open question is answered: **alandtse/CommonLibF4**, OG-only
(`ENABLE_FALLOUT_NG/VR=OFF`). Ryan-rsm-McKenzie's original has no `ProcessLists`, no `TES`
and no reference enumeration, so loaded actors cannot be enumerated with it at all.

## A-8 (2026-09-18) — The performance gate is met, measured

M0 and M1's filters, on a running game:

| | Sanctuary | Diamond City |
| --- | --- | --- |
| Actors | 75-78 | 49-50 |
| Candidates | 19-22 | 22-24 |
| Cost per pass | 0.004-0.015 ms | 0.012-0.028 ms |

Against the 0.25 ms/frame budget that is roughly 0.2 us per actor, and a pass has never
needed a second slice. `IsChild()` is confirmed to reach the engine's implementation
(Diamond City reports `child 2`, Sanctuary `child 0` because it has no children).

## A-9 (2026-09-18) — Names

| | |
| --- | --- |
| **Rapport** | The framework. It holds what the name says: the attraction stat, who has been paired with whom, cooldowns, and refusal memory. Addons ask Rapport what two people feel about each other. Plugin `Rapport.dll`, config `Data/F4SE/Plugins/Rapport.ini` and `Data/F4SE/Plugins/Rapport/*.json`, C++ namespace `RP`. |
| **Chemistry** | The NPC autonomy addon. Attraction, and the wasteland's other chemistry. |
| *(unnamed)* | The Player Proposals addon, replacing Sex 'Em Up. Named when it is started. |

"Autonomy" is deliberately not in any of these: AAF Autonomy Enhanced is what this replaces, and the
two should not be confused on a mod list. The earlier `AutonomyFramework.ini` and
`AutonomyFramework/*.json` paths in the Config section above are superseded by the Rapport names.

## A-10 (2026-09-18) — Aftermath, and taking over CumOverlays

Rapport owns **persistent aftermath**: overlays, body morphs and facial expressions that outlive the
scene that caused them. Both addons want it, so it is framework work rather than Chemistry's.

The division of labour with AAF is narrow and worth stating, because most of it already exists:

| | |
| --- | --- |
| AAF | `ApplyOverlaySet` / `RemoveOverlaySet`, `ApplyMorphSet`, `ApplyMFGSet`. An `overlayGroup` carries its own `duration` and `quantity`, so a timed, randomised overlay is pure XML. |
| Rapport | Which set, from what happened in the scene. And **persistence**: `{actor, setID, expiresAt}` in game time, in the co-save, removed on a tick — because AAF's timer does not survive a save, a reload, or the game closing mid-count, and an overlay stranded that way stays on the actor forever. |

Assets stay a third-party dependency, exactly like an animation pack. CumOverlays v1.4 supplies 57
LooksMenu templates and their textures; none covers the face, so anything aimed there needs a pack
that provides it.

**Rapport stops CumOverlays while its own aftermath is running**, rather than asking the player to
delete files. Two quests in `CumOverlays.esp` — `CumOverlay_Main` (`0x000802`) and
`CumOverlay_Starter` (`0x000803`) — and stopping them unfills the player alias, so nothing of that
mod runs. It is what the mod does to itself on a rollback, and it is reversible.

That creates one obligation, which is the price of the decision: **a stopped quest stays stopped.**
Rapport must start both again the moment its aftermath feature is switched off, and the uninstall
instructions must say to switch it off before removing Rapport. A player who removes Rapport with
the feature on has silently lost their cum overlays with no way to guess why.

Rejected: warning only, and opt-in-by-default. Both leave doubled overlays as the normal experience
until someone finds a switch, and the owner would rather carry the uninstall obligation than ship
that.

## A-11 (2026-09-18) — Takeover: Rapport manages the mods it depends on

A generalisation of the debug hub, from diagnostic settings to operational ones. Rapport configures
the mods it works alongside so the player does not have to know they needed configuring.

A-10 was the first instance — stopping CumOverlays while our aftermath runs — and there are more:
Autonomy Enhanced and Sex 'Em Up are replaced outright (A-2), and AAF has settings we depend on.

### The principle

**Automatic, but never secret.** The player should not have to configure it, and must still be able
to see it. A mod that silently changes another mod's settings is indistinguishable, from the
outside, from a mod that breaks it.

So every rule:

- **Detects before acting.** Nothing is touched unless it is actually installed.
- **Records the previous value** before changing it, so restoring is exact rather than a guess at
  what the default used to be.
- **Is listed in the UI**, with the reason, and can be switched off individually.
- **Names every change in the log**, the way the debug hub already does.

### The four mechanisms

| | How | Side |
| --- | --- | --- |
| Stop a quest | `Quest.Stop()` / `Start()` | Papyrus |
| Set a global | `TESGlobal::value` | Native — no round trip |
| Set an MCM setting | `MCM.SetModSetting*` by mod name | Papyrus |
| Write an ini | file, with a backup | Native, next launch |

The global route is worth knowing: a mod's MCM page often writes GlobalVariables rather than MCM
settings — Autonomy Enhanced's does (`"sourceType": "GlobalValue"`) — and those are directly
writable from the plugin.

### Why this waits for the co-save

Restoring a setting exactly means knowing what it was before Rapport touched it, and that has to
survive the session in which it was changed. Without the co-save, a takeover can only be undone
before the game closes — which is the same failure as A-10's stopped quest, multiplied by every rule.

So takeover lands immediately after the co-save, alongside aftermath, and not before.

## A-12 — the save is a ledger of facts, not of policy (2026-09-18)

The co-save records **what happened**: when an actor last finished a scene, who with, how many
times, when a request involving them was refused, and one `need` float carried on an addon's behalf
without the framework interpreting it. It records no cooldown, no threshold and no verdict.

"Too soon", "bored of this partner", "wants company" are policy and belong to Chemistry (D6). The
framework guarantees the facts survive a save; deciding what they mean is somebody else's job.

The one cooldown that exists — `CooldownHours`, 24 by default — belongs to the stand-in behaviour in
`Scheduler::FinishPass`, not to the framework, and it goes away with the stand-in.

Only a scene that ENDED is written. A failed request says nothing about two people beyond "not now",
and a watchdog firing means we do not know what happened at all — a guess written into a save
outlives the session that made it.

## A-13 — aftermath persists in game hours, and takes CumOverlays' job (2026-09-18)

Implements A-10. Rapport ships its own `overlaySetData` with **no `duration`**, so AAF applies and
never removes, and Rapport owns removal from the co-save in GAME hours (12 by default). See
`docs/aftermath.md` for why AAF's own timer cannot do this and why CumOverlays' two quests have to
be stopped rather than merely coexisted with.

Rapport ships **no textures**. It drives CumOverlays' 57 LooksMenu templates, which is a
redistribution question answered the only defensible way. If that mod is absent the sets resolve to
nothing, the log says so, and nothing else is affected.
