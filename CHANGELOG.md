# Changelog

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
