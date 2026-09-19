---
name: Bug report
about: Something Rapport did, or did not do, in game
title: ''
labels: bug
assignees: ''
---

<!--
Before anything else: does Rapport.log say something about this already? It is written to
Documents/My Games/Fallout4/F4SE/Rapport.log and it is deliberately loud — it names every mod
setting it changed, every quest it stopped, and every time AAF refused something. Two lines worth
searching for first:

  "THE BRIDGE HAS STOPPED POLLING"   -- a Papyrus stack died; the log says what it was doing
  "DEVELOPMENT profile"              -- diagnostics were left on; that is a known state, not a bug
-->

## What happened

<!-- One or two sentences. What you saw, not what you think caused it. -->

## What you expected instead

## How to reproduce it

<!-- If you cannot, say so. "It happened once and I cannot get it back" is a useful report as long
     as the log is attached; a lot of this mod's bugs only happen on the second load of a session. -->

1.
2.
3.

## Rapport.log  (REQUIRED)

<!--
ATTACH THE FILE. Do not paste a fragment: the interesting line is usually minutes before the symptom
and several subsystems away from it, and a trimmed log has lost it more often than not.

  Documents/My Games/Fallout4/F4SE/Rapport.log

If Papyrus logging was on, Papyrus.0.log from the same folder is worth attaching too — it is the
VM's own account of a stack dying, and Rapport cannot see that from the inside.

BEFORE YOU ATTACH: the log contains file paths from your machine, which usually include your Windows
user name. Check it and redact if you would rather not share that.
-->

## Runtime inventory  (REQUIRED)

<!-- Rapport prints most of this at startup; copy it from the log rather than from memory. A
     version remembered is a version that has been wrong in half of these reports. -->

| | |
| --- | --- |
| Rapport version | <!-- the `Rapport vX.Y.Z` line at the top of the log --> |
| Fallout 4 version | <!-- 1.10.163 only; anything else is refused at load --> |
| Game store | <!-- Steam / GOG / Epic — the binaries differ even at the same version number --> |
| F4SE version | <!-- and please attach f4se.log too, from the same folder --> |
| AAF version | |
| LooksMenu / F4EE version | |
| `Rapport_Moisturizer.esp` enabled? | <!-- yes / no / not installed --> |
| Aftermath backend | <!-- the `aftermath: using ...` line in the log --> |
| CumOverlays version | <!-- or "not installed" --> |
| Commonwealth Moisturizer version | <!-- or "not installed"; if installed, did you run BodySlide with Build Morphs ticked? --> |
| Addon driving Rapport | <!-- Chemistry, something else, or none — the log names it when it calls TakeOverDecisions --> |
| Animation packs installed | <!-- names are enough; this matters more than it sounds, because what a tree can do is entirely the pack author's --> |
| Approximate plugin count | |
| Mod manager | <!-- Vortex / MO2 / manual --> |

## Your Rapport.ini

<!-- Paste the whole file if you changed anything in it. Several defaults are load-bearing:
     MaxSceneSeconds, StaleFlagGraceSeconds, DriveFaces and PanicClear each change behaviour in a
     way that looks like a different bug. -->

```ini

```

## Is an NPC stuck?

<!-- Fill this in only if the symptom is "this NPC will not do anything any more". It is a known
     failure mode with a known cause: AAF stamps AAF_ActorBusy on StartScene and ONLY a scene ending
     clears it, so a request that died leaves that NPC refused by every AAF mod for the rest of the
     save — including by mods that have nothing to do with Rapport. -->

- Which NPC, and where:
- Did it start before or after installing Rapport:
- Does `Rapport.log` mention releasing them (`release`, `stale`, `busy`):

## Anything else

<!-- Screenshots help a lot for anything visible — faces, overlays, clothing. Several bugs in this
     mod were invisible to every check the plugin can make about itself and visible only on screen. -->
