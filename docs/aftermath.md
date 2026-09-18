# Aftermath — what a scene leaves behind

The feature the owner asked for in one sentence: *"characters should cum a lot at the end, and that
cum should be visible N amount of time after the scene on the character it landed on."*

Three of those words are the whole design. **A lot** is a quantity problem. **Visible N time after**
is a persistence problem. **On the character it landed on** is a routing problem. AAF can do exactly
one of the three, and not the important one.

## What AAF already does, and where it stops

AAF's `ApplyOverlaySet(actor, setID)` drives LooksMenu overlays, and an `overlaySetData` XML names
which templates a set draws from:

```xml
<overlaySet id="Belly">
  <condition>
    <overlayGroup duration="300" quantity="1">
      <overlay template="Belly_1" alpha="100" isFemale="true"/>
```

`quantity` picks that many at random. `duration` is AAF's own removal timer, and it is **an
in-session countdown**. It does not survive a save, a reload, or the game closing mid-count. An
overlay stranded by any of those stays on the actor for the rest of the playthrough, and nothing in
any log explains it.

So AAF can put cum on someone. It cannot keep it there for a day, and it cannot reliably take it off
again. That gap is the entire reason this is a framework feature.

## What Rapport does

**`Data/AAF/Rapport_overlayData.xml`** defines six sets — `Rapport_Vaginal`, `Rapport_Anal`,
`Rapport_Oral`, `Rapport_Back`, `Rapport_Body`, `Rapport_DP` — with **no `duration` attribute**. The
schema makes it optional (`common.xsd`, `overlayGroupType`) and the shipped `cum_overlaysData.xml`
says what omitting it means in its own comments: the overlays are added without timer-controlled
removal. AAF applies and never removes. Rapport removes.

Each set carries two conditions, `isFemale="true"` and `isFemale="false"`, so one set id resolves to
the right templates for whoever it lands on. Nothing in the code needs to know an actor's sex.

`quantity` is 2–3 rather than 1, drawing from a wider pool than the sets that ship with CumOverlays.
One overlay reads as a smudge; three reads as the thing that was asked for. LooksMenu will not apply
the same overlay twice to the same actor, so overlapping sets stack rather than collide.

**`Data/F4SE/Plugins/Rapport/aftermath.json`** maps AAF's animation tags to those sets.
`PenisToVagina` calls for `Rapport_Vaginal` and `Rapport_Body`; `PenisToMouth` for `Rapport_Oral` and
`Rapport_Body`; and a scene whose tags match nothing leaves nothing. That is what makes kissing and
foreplay free — they are not excluded, they simply have no entry.

The tags come from `OnAnimationStart`, argument 3, and they are the only thing that says what a scene
actually was. A scene plays several animations, so the bridge reports each one and the plugin
accumulates them; the decision is made once, when the scene ends.

**The co-save** holds `{formID, expiresAt, setID}` per standing overlay, in **game hours**. Game time
is the right unit and real time is the wrong one: a player who sleeps eight hours should not find it
unchanged, and one who stands still for ten minutes should. Twelve hours is the default — most of a
day, still there after the walk home, not permanent.

**The tick** removes what has expired and applies what has not been asked for yet this session.
Those are the same line of code because they are the same situation: the plugin starts from nothing
every launch and has no memory of having asked. An overlay whose owner is not currently loaded waits
— removal goes out regardless, application waits until they are in front of us, because asking AAF
about someone three cells away is a call that can neither be seen to work nor to fail.

## Why CumOverlays gets stopped

Rapport's sets draw from **CumOverlays' templates**. Rapport ships no textures and redistributes
nothing; it drives assets that mod already installed. Which is also why it must stop that mod's two
quests while it owns the feature (`takeover.json`, amendment A-11):

CumOverlays applies its own sets on AAF's 300-second timer, and **that timer removes templates, not
sets**. `Belly_2` removed on CumOverlays' behalf is `Belly_2` removed from under Rapport. The user
would watch cum vanish five minutes into a twelve-hour window, with nothing anywhere to say why.

So `CumOverlay_Main` (`CumOverlays.esp` `0x000802`) and `CumOverlay_Starter` (`0x000803`) are
stopped. Checked before being touched, named in the log with the reason, started again when the
feature is switched off. Nothing is deleted and no file of theirs is modified — the mod stays
installed, and its assets are exactly what Rapport is driving.

If CumOverlays is not installed, the sets resolve to nothing and the rest of the framework is
unaffected. That is a missing texture pack, not a broken dependency, and the log says so rather than
staying quiet about it.

## What is not solved yet

**Roles.** Tags say what an animation was, not who did what to whom. Both actors currently receive
the sets a scene called for; the female and male conditions route the templates correctly, but a
giver and a receiver are treated alike. AAF knows the roles — `GetActorData` and the position's own
role list — and reading them is the obvious next refinement.

**Faces.** There is no face template in CumOverlays' 57. `Rapport_Oral` therefore lands on the chest.

**Climax.** `requireClimax` exists and defaults to off, because many packs never tag `Climax` at all
and an honest default cannot assume they do.
