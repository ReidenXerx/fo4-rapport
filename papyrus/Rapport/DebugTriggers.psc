Scriptname Rapport:DebugTriggers Hidden
{R-23 (owner poll, 2026-09-24): scenes on demand, from the MCM's Debug page and its
hotkeys. fo4-mcp cannot start AAF scenes, so the owner starts them himself -- through
the same entry points a real scene uses.

Each trigger comes twice: FORCED skips the soft gates (the bar, cooldowns, privacy,
time of day); REAL obeys every gate the stand-in obeys and says which one refused.
The hard rules hold for both: adults only, alive, loaded, not fighting, a race
Rapport dresses, one scene at a time.

Global functions with no parameters, so an MCM button and a keybind can both call
them with CallGlobalFunction and nothing to type-convert.}

; How far "in front of you" reaches. Close for a scene with you -- you are looking
; at the one you mean -- and wider for somebody else's, who may be across the room.
Float Function WithMeDistance() Global
	Return 600.0
EndFunction

Float Function ForThemDistance() Global
	Return 1500.0
EndFunction

Function Say(String asLine) Global
	Debug.Notification(asLine)
	Rapport:Core.Trace("debug trigger: " + asLine)
EndFunction

Function SceneWithMe(Bool abForce) Global
	Actor target = Rapport:Core.ActorInFront(Rapport:DebugTriggers.WithMeDistance(), 35.0)
	If target == None
		Rapport:DebugTriggers.Say("Rapport debug: nobody in front of you - face them, within a few steps")
		Return
	EndIf
	Rapport:DebugTriggers.Say(Rapport:Core.DebugSceneWith(Game.GetPlayer(), target, abForce))
EndFunction

Function SceneForThem(Bool abForce) Global
	Actor target = Rapport:Core.ActorInFront(Rapport:DebugTriggers.ForThemDistance(), 25.0)
	If target == None
		Rapport:DebugTriggers.Say("Rapport debug: nobody in front of you - face the one you mean")
		Return
	EndIf
	Rapport:DebugTriggers.Say(Rapport:Core.DebugSceneFor(target, abForce))
EndFunction

; ---- what the MCM calls -----------------------------------------------------
Function SceneWithMeForced() Global
	Rapport:DebugTriggers.SceneWithMe(true)
EndFunction

Function SceneWithMeReal() Global
	Rapport:DebugTriggers.SceneWithMe(false)
EndFunction

Function SceneForThemForced() Global
	Rapport:DebugTriggers.SceneForThem(true)
EndFunction

Function SceneForThemReal() Global
	Rapport:DebugTriggers.SceneForThem(false)
EndFunction
