Scriptname Rapport:Medic extends Quest
{The thing that is still running when the bridge is not.

 Rapport has four watchdogs and three of them cure what they find. The fourth --
 the one that notices the bridge has stopped polling -- could only ever REPORT,
 because every cure this framework has is delivered as an Order and Orders are
 delivered by the bridge polling. When the poll is what died, the cure cannot be
 posted through the thing that is broken.

 So this script exists on its own quest, for two reasons, both structural:

   A stuck Papyrus stack blocks every later event ON THAT SCRIPT. The bridge is
   the script that calls AAF, so the bridge is the script that can get stuck, and
   a watchdog inside it would be stuck alongside it -- silent at exactly the
   moment it had something to say.

   A separate QUEST means this can stop and start the bridge's quest to get a
   fresh script instance, without resetting itself while doing so.

 IT NEVER CALLS AAF, AND IT NEVER CALLS INTO THE BRIDGE AND WAITS. Both rules
 are what keep it alive: the first is how the bridge dies, and the second would
 queue this script's stack behind the stuck one and kill it the same way. When it
 does need the bridge to do something, it uses CallFunctionNoWait -- which does
 not wait, so a bridge that never runs it costs this script nothing.

 Style note: the base sources are decompiled and so carry no default argument
 values. Every argument is passed explicitly. See docs/papyrus-toolchain.md.}

; The medic's own clock, on its own script, deliberately unrelated to the
; bridge's. Ten seconds: short enough that a wedge is caught inside one plugin
; tick of it being real, long enough to be free.
Int Property kMedicTimer = 1 AutoReadOnly
Float Property kBeatSeconds = 10.0 AutoReadOnly

; How many plugin ticks of silence before anything is done. The plugin ticks
; every 20s, so 2 is roughly 40 seconds of a bridge that has not asked once.
; ONE is not enough: a save and a fast travel's loading screen both freeze the VM
; for longer than a tick, and the bridge comes back on its own. Measured
; 2026-09-20 -- a 46-second gap across a load healed ten seconds later, and an
; alarm on one tick would have called an ordinary fast travel a failure.
Int Property kSilentTicks = 2 AutoReadOnly

; Whether the cheap cure has already been tried against THIS silence. The
; escalation is the whole design: a re-arm cannot make anything worse, so it goes
; first and costs nothing when the bridge was merely busy; the expensive cure
; only happens to a bridge that did not come back from it.
Bool _rearmTried = false

;---------------------------------------------------------------------------

Event OnQuestInit()
	Self.Beat()
EndEvent

Event OnInit()
	Self.Beat()
EndEvent

Function Beat()
	Self.StartTimer(kBeatSeconds, kMedicTimer)
EndFunction

Event OnTimer(Int aiTimerID)
	If aiTimerID != kMedicTimer
		Return
	EndIf

	; FIRST, before anything else, exactly as the bridge does it. A handler that
	; schedules at the end is a handler that stops scheduling the moment anything
	; in it goes wrong -- and this is the script that must not stop.
	Self.StartTimer(kBeatSeconds, kMedicTimer)

	Int silent = Rapport:Core.BridgeSilentTicks()
	If silent < kSilentTicks
		; Alive. Whatever was wrong is not wrong now, so the next silence starts
		; its escalation from the beginning rather than from wherever this one
		; got to.
		_rearmTried = false
		Return
	EndIf

	If !_rearmTried
		_rearmTried = true
		Rapport:Core.Trace("medic: the bridge has not polled for " + silent + " tick(s) - re-arming its clock")
		Self.ReArmTheBridge()
		Return
	EndIf

	; Still silent after a re-arm. The clock was not the problem: the stack is
	; stuck, and a stuck stack cannot be un-stuck from Papyrus. What CAN be done
	; is take away everything it was holding, and hand the quest a fresh script
	; instance to start over with.
	_rearmTried = false
	Rapport:Core.Trace("medic: still silent after a re-arm - giving up on what the bridge was holding")

	If Rapport:Core.AbandonInFlight("the bridge stopped polling and did not come back after a re-arm")
		; Only when something was ACTUALLY given up on. A re-arm is nobody's
		; business but ours; two NPCs snapping out of an animation is something
		; the player can see, and a player who sees it without being told assumes
		; the mod broke.
		Debug.Notification("Rapport: a scene stopped responding - recovering")
	EndIf

	Self.RestartTheBridge()
EndEvent

;---------------------------------------------------------------------------
; The two cures
;---------------------------------------------------------------------------

; The cheap one: the bridge is fine but its clock is gone. CallFunctionNoWait
; because a bridge whose stack is stuck would otherwise take this stack down with
; it -- the call is posted and forgotten, and a bridge that never runs it simply
; fails to come back, which is the case the expensive cure is for.
Function ReArmTheBridge()
	Rapport:Bridge bridge = Self.TheBridge()
	If bridge == None
		Return
	EndIf
	bridge.CallFunctionNoWait("ReArm", new Var[0])
EndFunction

; The expensive one. Stopping and starting the quest gives the script a fresh
; start: OnQuestInit runs again, Connect() re-registers every AAF event and
; re-arms the poll, and anything the old instance was in the middle of is gone
; along with it.
Function RestartTheBridge()
	Quest bridgeQuest = Self.TheBridgeQuest()
	If bridgeQuest == None
		Rapport:Core.Trace("medic: the bridge quest does not resolve - cannot restart it")
		Return
	EndIf

	bridgeQuest.Stop()
	bridgeQuest.Start()
	Rapport:Core.Trace("medic: stopped and started the bridge quest - it starts over from here")
EndFunction

;---------------------------------------------------------------------------

Quest Function TheBridgeQuest()
	; 0x800 is the first object id a new plugin may use, and the bridge quest has
	; it. Asked by file so the answer is right whatever the load order is.
	Return Game.GetFormFromFile(0x00000800, "Rapport.esp") as Quest
EndFunction

Rapport:Bridge Function TheBridge()
	Quest bridgeQuest = Self.TheBridgeQuest()
	If bridgeQuest == None
		Rapport:Core.Trace("medic: the bridge quest does not resolve")
		Return None
	EndIf

	; A failed cast assigns None rather than throwing, and None is what every
	; caller here checks for. Writing it out instead of chaining the casts is
	; deliberate: a cast that silently produces None inside an expression is how
	; this codebase once got an infinite loop.
	Rapport:Bridge bridge = bridgeQuest as Rapport:Bridge
	If bridge == None
		Rapport:Core.Trace("medic: the bridge quest resolves but carries no Rapport:Bridge script")
	EndIf
	Return bridge
EndFunction
