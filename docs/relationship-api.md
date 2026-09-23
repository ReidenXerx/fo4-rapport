# Relationship API: what an addon can read and write

Rapport keeps one relationship per pair of people who have *interacted*: a scene, dialogue, a gift.
It stores a **bond** from -1 (enemies) to +1 (closest), in Rapport's co-save. What the bond is
*worth* (how much likelier a scene gets, which line a greeting picks) is the consumer's curve, not
Rapport's (R-10). Chemistry's curve is in its DESIGN.md, C-3.

Design and decisions: `relationship-and-personas.md` (R-1 to R-14).

## Prefer `Rapport:Relations` when you hold both Actors

| Function | Returns | Use |
| --- | --- | --- |
| `Rapport:Relations.BondBetween(Actor a, Actor b)` | Float -1..+1 | **Ranking pairs.** The stored bond once the pair has interacted. Before that, what the engine's relationship *would* seed, and nothing is written (R-5). |
| `Rapport:Relations.AddBondBetween(Actor a, Actor b, Float amount, Int reason)` | the new bond | **Changing a bond.** Imports the engine's relationship first, exactly as a scene does, so the pair keeps its starting point. |
| `Rapport:Relations.AreBloodRelated(Actor a, Actor b)` | Bool | The engine's blood associations, live |
| `Rapport:Relations.ArePartners(Actor a, Actor b)` | Bool | Spouse or courting, live |

`amount` moves the bond by that **fraction of the distance left** toward +1 (or -1 when negative), so
it cannot overshoot, and a large bond grows slowly. `reason`: 3 dialogue, 4 gift, 5 any other addon.

## By form id (`Rapport:Core`)

| Function | Notes |
| --- | --- |
| `Float PairBond(Int a, Int b)` | The stored bond, 0 when there is no record. Order-independent. |
| `Float AddBond(Int a, Int b, Float amount, Int reason)` | Does **not** import the engine's relationship. Use `AddBondBetween` when you have Actors. |
| `Bool IsPairSeeded(Int a, Int b)` | True once the engine's relationship was imported (the pair's first interaction) |
| `Bool IsIncestPair(Int a, Int b)` | Stored flag: blood relatives (siblings, parent/child, grandparents, aunts/uncles, cousins; in-laws are not blood) |
| `Bool IsPartnerPair(Int a, Int b)` | Stored flag: spouse or courting |
| `Float SeedBond(Int rank, Bool partner)` | The starting-bond formula on its own, stores nothing |
| `Int PairSceneCount(Int a, Int b)` | Scenes the pair has had together |
| `Bool IsAffairPair(Int a, Int b)` / `NoteAffair(Int a, Int b)` | One of them was partnered elsewhere when they had a scene. A future "bad thing". |
| `Float FaithfulnessOf(Int formID)` | 0 (strays freely) to 1 (never), derived from the form id |
| `Bool Rapport:Relations.HasPartner(Actor a)` | Married or courting anyone |
| `String PersonaOf(Int formID)` | `mercantile`, `romantic`, `vulgar` or `reticent`, or the owner's pin from `personas.json` |

## What the numbers are

Measured in game, R-4:

| The engine says | `GetRelationshipRank` | Starting bond |
| --- | --- | --- |
| Spouses (the Codmans) | 4 | +0.80 (rank x 0.15, capped at 0.60, +0.20 for a partner) |
| Siblings, parent and child | 3 | +0.45 |
| Employer and employee | 1 | +0.15 |
| Strangers | 0 | 0 |

Every scene adds 15% of the distance left toward +1. On death, the dead actor's records are dropped.

## The incest flag is a flag, never a refusal

Owner's decision, R-14. Nothing in Rapport or Chemistry refuses a blood pair. The flag exists so that
a consumer that *judges* can read it. The planned attitude layer, where people who know treat it as
a bad thing, is the first. If your addon refuses on it, that is your addon's choice. Rapport's
default is not to.

## Personas

Every NPC's persona comes from its form id: stable, costs nothing to save, and the same on every
machine (R-7). The owner can pin one in `Data/F4SE/Plugins/Rapport/personas.json`:

```json
{ "overrides": [ { "plugin": "CompanionIvy.esm", "id": "000803", "persona": "vulgar" } ] }
```

`plugin` + `id` are what xEdit shows, without the load-order byte. Either the actor reference or its
base NPC works. A pin naming a plugin this player does not have is skipped quietly. A pin that
resolves to anything other than an actor or NPC (a voice type, say) is refused with a warning.

Chemistry's persona rules (who pairs, which story) are its DESIGN.md C-8.

## Checking it from outside the game (dev builds)

`rank <a> <b>` on Rapport's command channel traces the engine's rank both ways, blood, partner,
whether the pair is stored, and `BondBetween`, into `Rapport.log`. `bond <a> <b> [add x]` reads or
moves the stored bond.

## Being narrated

If your addon decides scenes, tell the Narrator your share just before `RequestScene`, and it will
appear in the numbers line after Rapport's own parts:

```papyrus
Rapport:Core.NarrateBonus(first, second, "bond", 0.45)      ; labels are lower-cased
Rapport:Core.NarrateBonus(first, second, "couple", 0.0)     ; a zero is a marker, not printed
Rapport:Core.RequestScene(akFirst, akSecond, "athome")
```

When you pass on a likely pair, `NarrateNearMiss(first, second, "too many people are watching",
score, bar)` gives the "why nothing happened" line. Rapport rate-limits it and adds the names.

Your own moments, in your own words (ApiVersion 201+): `NarrateLine(first, second, headline,
numbers)`. `{first}` and `{second}` in either text become the two names. The numbers line only
shows when the player has the numbers switch on, and the whole line obeys the "addon moments"
switch. Overture says how a conversation went:

```papyrus
Rapport:Core.NarrateLine(player, npc, "{second} liked that. Not here, though.", "bond +0.08")
```

Write it the way the Narrator talks: one sentence, in the world, no percentages in the headline.

## Lovers by an addon's word

`Rapport:Core.SetLovers(first, second, True)` (ApiVersion 201+) records two people as lovers
because your mod says so. Overture does it for the player and an NPC after a yes. It is kept
apart from the engine's spouse and courting: `Rapport:Relations.ArePartners` and `HasPartner`
answer exactly what they did before. Ask `Rapport:Relations.AreLovers(a, b)` or `HasLover(a)` as
well wherever a lover should count. Chemistry, for one, counts the player's lover as spoken for,
behind its own MCM switch. It is kept in the save, and a death forgets it.

## The player's priority lane

Rapport runs one scene at a time, and an autonomous mod like Chemistry can take the slot in the
seconds between a player's "yes" and your request. `Rapport:Core.ReservePlayerScene(akWith,
afSeconds)` (ApiVersion 201+) holds it for the player and `akWith` for up to 120 s: every other
pair's `RequestScene` is refused until your pair's own request is accepted, which lets the hold
go by itself. Pass 0 to let go early (the conversation ended without a yes). A scene already
running is never cut short; the hold takes the next free slot. It only ever holds for a pair with
the player in it: a deliberate player request outranks autonomy, and nothing else does.

## Names for the nameless

`Rapport:Core.Introduce(akActor)` (ApiVersion 201+) gives a generic NPC a first name and a
surname, and returns it. Generic means the name they go by is a label: carried by five or more NPC
records, not all of them Unique ("Drifter", "Settler", "Resident"). A Unique NPC counts only when
that name comes from their template rather than their own record, which is how some mods flag every
patron Unique and still call them all "Drifter". It returns an empty string if they have a real
name, already wear a custom one, have ever been the player's companion, were introduced before, or
the player turned names off. `Rapport.log` says which ("keeps their own name (...)"), and
`Rapport-labels.txt` beside it lists every label with its record counts. The name is derived from the form id, so the same person always gets the same one; the save
only keeps who was introduced. Call it when your mod has a reason for the player to learn a name,
not on every NPC in sight.

