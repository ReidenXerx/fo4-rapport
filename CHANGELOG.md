# Changelog

## 0.2.1 (unreleased)

- **Dialogue in a borrowed voice.** When an addon's conversation line (Overture's) has no
  recording in the speaker's own voice, a named NPC like Mayor McDonough used to answer in
  subtitles only. Now the line is looked up again in the closest voice that addon did record,
  chosen by the same voice fingerprints as the barks (the Mayor gets MaleRough). This only
  happens for the addon's own lines, and only when the file is missing. The game's lines and
  every other mod's lines are left alone. Voices that must stay silent (children, robots,
  creatures) still do.
- **Sexual orientation.** Every NPC is straight, bi or gay. Seven in ten are straight, two in
  ten bi and one in ten gay, and the same person is always the same, on every machine and
  every save. Rapport never pairs two people who would not want each other. There's no
  bond that bends this. The companions you can romance are open to you whatever your sex,
  just as in the base game, and have an orientation of their own toward everyone else. A
  character's orientation can be set by hand in `personas.json`
  (`"orientation": "straight" | "bi" | "gay"`, `"playersexual": true`). Scenes started from
  the AAF menu are never refused over it. Addons read it through `OrientationOf` and
  `Attracted`.

- **Names for the nameless.** The first time a mod built on Rapport introduces someone who
  has no name of their own (a Settler, a Drifter, a Diamond City Resident), they get a first
  name and a surname. The same person always gets the same name, and it survives saves. People
  with real names keep them, and so does anyone who has ever been your companion. Rapport tells
  a name from a label by counting records, and writes what it decided to `Rapport-labels.txt`
  next to its log. If it gets one wrong for your load order, list the name under `"keep"` or
  `"label"` in `names.json`. There's a switch in MCM; `names.json` can also replace the name
  lists.
- **The Narrator says "you".** Lines about your own moments say "you and Dottie", never your
  name in the third person.
- **The Narrator speaks for addons too.** A mod can now narrate its own moments in its own
  words ("addon moments", on by default, a switch in MCM). Overture uses it to say how a
  conversation went, and why.
- **Scenes with the player.** The player never speaks a scene line: they have no persona, and
  their side stays their own. The NPC still says theirs. A scene the player asked for is narrated
  in the NPC's voice. If it fails after it was accepted, the Narrator says so instead of leaving a
  yes hanging. And a mod can hold the one scene slot for the player's own request, so autonomy
  can't take it in the moment between the yes and the scene.
- **Lovers.** A mod can declare two people lovers (Overture does, for you and the NPC). They're
  kept in your save, a death ends it, and so does falling out.
- **Fixes.**
  - The MCM menu never saved anything: MCM rejected every setting because its name lacked a
    type letter. Every setting now registers and keeps its value.
  - A body morph AAF sometimes leaves behind after a scene (it keeps body mods away from
    that NPC) is now removed from everyone Rapport had in a scene. If AAF's morph was all
    they had, body mods give them their body again after the next save and load. Rapport
    never rebuilds a body itself.
  - Right after a load, Rapport no longer offers pairs or counts onlookers from the place
    you just left.
  - After AAF refused a scene, Rapport lost track of every scene that followed, until the next
    load. They played, but wrote no bond and no history, and the slot looked busy for thirteen
    minutes at a time. The refused request is now dropped at once.
  - An NPC who had just started talking to you, or to someone else, could be picked for a scene
    in the seconds between Rapport's scan and the request. Rapport now checks again at the moment
    it asks.
  - Your own scene history no longer ages out of the save after a month without a scene.

**For addon authors:** `Rapport:Core.ApiVersion()` is 201. New: `NarrateLine` (your own
narrator line, with `{first}` and `{second}` for the names), `Introduce` (names a nameless
NPC, returns the name), `ObserversNear` (how many people could see an actor),
`ReservePlayerScene` and `PlayerHoldsSlot` (the player's priority lane), and `SetLovers` /
`AreLovers` / `LoverOf` / `LoverCount` / `LoverAt` (lovers by your mod's word, kept apart from the
engine's spouses), and the
bridge's `OnPlayerSceneRecorded` event (a scene with the player has ended and been recorded).
`NarrateLine` fills `{they}`, `{them}` and `{their}` for you, in any casing. Check for 201 before calling them: Rapport 0.2.0 doesn't have them. `RequestScene` now also refuses a pair where someone is dead, not loaded, in an ambient conversation or talking to the player (the player's own request excepted), and a pause never refuses a pair with the player in it.

## 0.2.0

**People have relationships now, NPCs talk, and you can see why things happen.**

- **Voiced scenes.** The two in a scene say something as it starts, in one of four personas
  (mercantile, romantic, vulgar, reticent) and in the voice their character already uses.
  211 lines across 32 voice types. NPCs with unique voices borrow the closest voice we
  recorded, matched by voice fingerprint.
- **Bystanders react.** People nearby turn their heads. If they can see it, or hear it
  through a wall, they may comment.
- **Relationships.** Every pair who has interacted has a bond from -1 to +1. It starts from
  the game's own relationships (spouses, family, friends, co-workers), and each scene
  together raises it. It is kept in your save, and a death clears it. Blood relatives and
  partners are recorded, and so is an affair when someone who is spoken for strays.
- **Personas and faithfulness.** Every NPC has a persona and a faithfulness, fixed per NPC
  and stable across load orders. `personas.json` can pin a persona by hand. Ivy is vulgar.
- **The Narrator (optional, on by default).** A HUD line saying who is about to have a scene
  and why, then the score part by part. In MCM you can also turn on near misses,
  relationship turns and bystanders, and read the recent history.
- **MCM menu.** Scene choice, voices and the Narrator. Changes apply within 20 seconds.
  MCM stays optional.
- **Only your part of the world.** Nobody in a cell you have left is scored, counted as a
  witness, or started in a scene the game is no longer running. Only people count as
  onlookers: brahmin and turrets no longer do. Settlement life (WorkshopParent and the
  Sanctuary group) no longer keeps settlers out of scenes.
- **Fixes.**
  - A save loaded mid-scene no longer blocks every later request.
  - No AAF call is made during a loading screen, which crashed Scaleform.

**Saves:** 0.1.x saves load fine; their pair history starts with a bond of 0. A save made
on 0.2.0 loaded with 0.1.x loses the relationship table, so don't downgrade.

**For addon authors:** see `docs/relationship-api.md`. There is `Rapport:Core.ApiVersion()`
(this release is 200), `Rapport:Relations.BondBetween` / `AddBondBetween`, and
`NarrateBonus` / `NarrateNearMiss` so the Narrator can explain your mod's decisions too.

## 0.1.1

**Diagnostics were shipping enabled. They are off now.**

`debug.json` ships with `active: "debug"` because that is the right default on a
development machine, and the release packaging copied the config folder verbatim — so
the 0.1.0 archive carried it. That profile turns on **Papyrus tracing**, which slows the
script engine, and writes to your `Fallout4Custom.ini`.

If you installed 0.1.0, updating fixes it. Nothing you need to do by hand; the config is
replaced with `active: "off"`.

Packaging now forces the shipped copy to `off` and **reads it back**, refusing to build a
release that would ship diagnostics on — and checks the file has no byte-order mark,
because the first version of that fix added one and a BOM is not JSON to the plugin's
parser.

No gameplay changes.

## 0.1.0

First release.
