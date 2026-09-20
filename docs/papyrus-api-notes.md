# Papyrus API notes — what the engine already has

The reconstructed base sources live at `D:\F4CustomMods\PapyrusBase\Source\Base` — **9658 `.psc`
files**, and `scripts/build-papyrus.ps1` already compiles against them. They are the reference for
every native in the game. They were treated as undocumented for most of a development session while
sitting in this project's own build command.

**They are decompiled, so they carry no default argument values.** Every argument must be passed
explicitly; omitting one is a compile error, not a silent default. `EnableAI(Bool abEnable, Bool
abPauseVoice)` cost a failed build learning that.

Everything below was read from those sources and, where noted, **measured in game**. Nothing here is
inferred from a function's name.

## The ones that were hand-rolled first, and should not have been

| Wanted | Hand-rolled | The engine already had |
| --- | --- | --- |
| Heading from A to B | `atan2` with Bethesda's clockwise-from-+Y convention, head-height correction | `ObjectReference.GetHeadingAngle(ObjectReference akOther)` |
| Read the game clock | `RE::Calendar::GetHoursPassed` in C++ | `Utility.GetCurrentGameTime()` |
| Advance the clock | nothing — the aftermath window was untestable | `Game.PassTime(Int aiHours)` |
| Not land inside a wall | a distance band, tuned until the symptom moved | `ObjectReference.MoveToNearestNavmeshLocation()` |

## Measured behaviour — not what the names suggest

**`Game.SetAngle` on the player CRASHES THE GAME.** 2026-09-20 02:59:52 it ran with pitch -0.13, yaw
104.63; 02:59:53 the game died on a null function-pointer call. The stack was the player
repositioning path: `GameVM::ProcessEvent(PositionPlayerEvent&)`,
`BSTEventSource<PositionPlayerEvent>::Notify`, `ForceFullUpdate`, `DispatchRenderSafeCalls`, with
`PlayerCharacter` and `BGSLocation "Diamond City"` among the relevant objects. Rotating the player
raises `PositionPlayerEvent` and the engine does a full reposition off the back of it — which also
produced a loading screen from a command that had explicitly not teleported anybody.

`MoveTo` on the player is fine and is proven working. It is ROTATION that is the problem, not
movement; do not generalise the crash into "never touch the player".

**`Game.SetCameraTarget(Actor)` ignores the actor you pass it.** Tried in game: it forces third
person, and aiming it at two different actors produced BYTE-IDENTICAL frames. Whatever it is for, it
is not per-actor framing.

**`Game.PassTime(Int aiHours)` DOES NOT ADVANCE HOURS.** Measured 2026-09-20 03:42: the clock read
781.54 before a single `PassTime(1)` and 881.55 after — **about 100 game hours, four days**, from a
parameter named `aiHours`. Confirmed on a second reading and corroborated by an independent clock
(Rapport's ledger, 781.3). Whatever the argument means, it is not hours.

This one is the cautionary tale for this whole document: the signature was correct, the survey did
its job, and the function was still shipped on the strength of a NAME. A signature tells you how to
call something, never what it does. Use `Utility.GetCurrentGameTime()` before and after ANY time
call, on a throwaway save, starting from the smallest value.

**`MoveTo` ACROSS A WORLDSPACE LEAVES THE LOADING SCREEN UP FOREVER.** Measured 2026-09-20 04:05,
Diamond City -> Goodneighbor: the move itself succeeded, `nearby` listed the Goodneighbor NPCs, the
poll kept turning, the plugin kept answering — and the screen stayed on the loading art
indefinitely. Everything worked except the thing the player could see.

MoveTo does not perform the transition the engine waits on. `Game.FastTravel(ObjectReference)` does,
and it accepts any reference, so you can travel to a PERSON without needing a map marker's form id.
The rule is the owner's: **FAST TRAVEL FIRST, THEN MoveTo for precision** — two operations, the same
shape as the same-cell / cross-cell split, and using either alone is the mistake.

`reload` recovers a stuck loading screen, which is worth knowing independently.

**There is NO Papyrus route to the console.** Searched across the corpus for
`ExecuteConsoleCommand`, `ConsoleCommand`, a console-wrapping script class, and `Console*.psc` — all
zero. The 23 files matching "Console" are in-world terminal objects. The cheat EFFECTS are reachable
as ordinary natives (below); arbitrary command strings are not.

## CONFIRMED WORKING — aiming the camera

`Game.StartDialogueCameraOrCenterOnTarget(ObjectReference)` **centres the camera on the actor you
name**, and `StopDialogueCamera(False, True)` + `ForceFirstPerson()` gives it back. Tried in game
2026-09-20 03:39, confirmed by the owner watching their own screen, and again on release at 03:40.
No crash, and the view visibly follows the named target — which is exactly what SetCameraTarget did
not do.

It took three attempts to get here and only the third was built on a function whose documented
purpose matched the operation:

1. `SetAngle` on the player, with hand-rolled trigonometry — **crashed the game**
2. `SetCameraTarget(Actor)` — inert; forces third person, ignores the actor
3. `StartDialogueCameraOrCenterOnTarget(ObjectReference)` — **works**

**`GetHeadingAngle` is RELATIVE, not absolute.** It returns the angle from the caller's CURRENT
FACING to the target, not a world yaw. Measured: `state` reported `heading=108deg` for an actor 333
units away while the absolute world yaw to them was about 104. Feeding it into `SetAngle` would have
mixed a relative angle into an absolute field and aimed at nothing — so the plan that replaced the
crashing one would also have been wrong, for a different reason.

## Verified signatures worth keeping

Camera:
```papyrus
Function StartDialogueCameraOrCenterOnTarget(ObjectReference akCameraTarget) Global Native
Function StopDialogueCamera(Bool abConsiderResume, Bool abSwitchingTo1stP) Global Native
Function ForceFirstPerson() Global Native
Function ForceThirdPerson() Global Native
Function PlayEventCamera(camerashot akCamera, ObjectReference akRef) Global Native
```

Placement:
```papyrus
Function MoveTo(ObjectReference akTarget, Float afXOffset, Float afYOffset, Float afZOffset, Bool abMatchRotation) Native
Function MoveToNearestNavmeshLocation() Native
Function MoveToNode(ObjectReference akTarget, String asNodeName, String asMatchNodeName) Native
Function FastTravel(ObjectReference akDestination) Global Native
```

Time:
```papyrus
Float  Function GetCurrentGameTime() Global Native      ; Utility
String Function GameTimeToString(Float afGameTime) Global Native
Function WaitGameTime(Float afHours) Global Native
Function PassTime(Int aiHours) Global Native            ; Game
```

Cheat effects, all on `Debug`, all Global:
```papyrus
Function SetGodMode(Bool abGodMode) Global Native
Function EnableCollisions(Bool abEnable) Global Native
Function EnableDetection(Bool abEnable) Global Native
Function EnableMenus(Bool abEnable) Global Native
Function EnableAI(Bool abEnable) Global Native          ; ONE arg, and global
```
`Actor.EnableAI(Bool abEnable, Bool abPauseVoice)` is a DIFFERENT function — two arguments, instance
method. Do not conflate them.

Actor state, all instance methods, all read-only:
```papyrus
Bool Function IsInScene()        Bool Function IsInCombat()      Bool Function IsTalking()
Bool Function IsWeaponDrawn()    Bool Function IsSneaking()      Bool Function IsDead()
Bool Function IsUnconscious()    Int  Function GetSitState()     Int  Function GetSleepState()
Int  Function GetRelationshipRank(Actor akOther)
Actor Function GetDialogueTarget()   Package Function GetCurrentPackage()
Bool Function Is3DLoaded()       Scene Function GetCurrentScene()
Float Function getDistance(ObjectReference akOther)   ; lowercase in the source, not a typo
Float Function GetHeadingAngle(ObjectReference akOther)
```

Holding an actor still:
```papyrus
Function EnableAI(Bool abEnable, Bool abPauseVoice) Native
Bool Function SetRestrained(Bool abRestrained) Native
Function SetHeadTracking(Bool abEnable) Native
Function SetLookAt(ObjectReference akTarget, Bool abPathingLookAt) Native
Function ClearLookAt() Native
Bool Function SnapIntoInteraction(ObjectReference akTarget) Native
```

Player controls — `InputEnableLayer`, an instance class created with `InputEnableLayer.Create()`:
```papyrus
Function DisablePlayerControls(Bool abMovement, Bool abFighting, Bool abCamSwitch, Bool abLooking, Bool abSneaking, Bool abMenu, Bool abActivate, Bool abJournalTabs, Bool abVATS, Bool abFavorites, Bool abRunning) Native
Bool Function IsInMenuMode() Global Native     ; Utility
```

## Worth following up

`GetRelationshipRank(Actor)` is an engine-held relationship value between two actors. Chemistry
scores pairs on distance, privacy, observers, time of day and faction, and has never consulted it.
That is a real input sitting unused.

## How this was found

Six subagents, one per capability domain, sweeping by CAPABILITY rather than by name — about 590,000
tokens of searching that returned six pages. Searching for "camera" finds `SetCameraTarget`;
searching for "how do you point a view at a thing" finds `StartDialogueCameraOrCenterOnTarget` and
`GetHeadingAngle` too.

One of them checked the corpus size it had been given and corrected it: 9658 files, not the 1795 it
was told. Another therefore scoped its search to the top level only, so its "no such function"
results cover 1795 files rather than the tree — a narrower negative than it looks.
