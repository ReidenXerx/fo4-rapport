Scriptname Rapport:Moisturizer extends Quest
{Rapport's half of Commonwealth Moisturizer.

 This is a SEPARATE script in a SEPARATE plugin, and that is the whole point of
 it. Naming a CMkz type inside Rapport:Bridge would put an unresolvable script
 reference into the bridge on every install that does not have Moisturizer, and
 the bridge is the one script the entire mod depends on. Everything that touches
 that mod lives here instead, in Rapport_Moisturizer.esp, which is only installed
 by people who have it.

 What Moisturizer does that CumOverlays cannot: its cum is worn geometry, not a
 flat texture -- a BodySlide-conformed mesh on an armour slot, plus morphing
 headparts for the face. It is the only thing on the machine that does faces.

 What it does not do is outlive a session reliably: its own timer is real-time
 minutes, and its MCM offers 0 to mean "no timer". So Rapport sets it to 0, stops
 its AAF listener, and owns the whole lifecycle itself -- applied from Rapport's
 tag routing, removed from Rapport's co-save in GAME hours.

 Style note: the base sources are decompiled and carry no default argument
 values, so every argument is passed explicitly. See docs/papyrus-toolchain.md.}

Int Property kPollTimer = 1 AutoReadOnly

; Its library quest. ComMoisturizer.esp|F99, the same form its own MCM calls.
Int Property kLibFormID = 0x00000F99 AutoReadOnly

; The two duration sliders, in minutes. Zero means no timer, which is exactly
; what Rapport wants: removal is ours, from the save, in game hours.
Int Property kDurationPlayerFormID = 0x00000806 AutoReadOnly
Int Property kDurationNPCFormID = 0x00000AD5 AutoReadOnly

CMkz:CMkz_LibScript _lib

;---------------------------------------------------------------------------
; Startup
;---------------------------------------------------------------------------

Event OnQuestInit()
	Self.Connect()
EndEvent

Event OnInit()
	Self.Connect()
EndEvent

Function Connect()
	; Same lifetime problem as the bridge has: this script's variables live in the
	; save while Rapport.dll starts from nothing every launch, so nothing here may
	; be guarded on a remembered flag. The plugin is asked instead.
	_lib = Game.GetFormFromFile(kLibFormID, "ComMoisturizer.esp") as CMkz:CMkz_LibScript

	If _lib == None
		Rapport:Core.Trace("moisturizer: ComMoisturizer.esp is not loaded - this plugin does nothing")
		Return
	EndIf

	Rapport:Core.Trace("moisturizer: connected to the CMkz library")
	Self.NeutraliseOwnTimer()
	; Cancel first. OnQuestInit and OnInit both call Connect, and the first live
	; run started three overlapping polls in the moisturizer because each one
	; started another timer. Papyrus cannot be asked whether a timer is already
	; running, and a remembered flag is the thing that has wedged this mod twice,
	; so cancelling unconditionally is the answer that needs no state at all.
	Self.CancelTimer(kPollTimer)
	Self.StartTimer(Rapport:Core.PollSeconds(), kPollTimer)
EndFunction

; Its timer is real-time minutes and cannot survive a save; ours is game hours in
; a co-save and can. Zero is its own documented value for "no timer", so this
; uses the escape hatch the mod already provides rather than fighting it.
;
; A GlobalVariable is a vanilla type, so reading these needs no CMkz reference at
; all -- which is why the neutralising could have lived in the bridge and the
; API calls could not.
Function NeutraliseOwnTimer()
	If !Rapport:Core.MoisturizerWanted()
		Return
	EndIf

	GlobalVariable player = Game.GetFormFromFile(kDurationPlayerFormID, "ComMoisturizer.esp") as GlobalVariable
	GlobalVariable npc = Game.GetFormFromFile(kDurationNPCFormID, "ComMoisturizer.esp") as GlobalVariable

	If player != None && player.GetValue() != 0.0
		Rapport:Core.Trace("moisturizer: its player timer was " + player.GetValue() + " minute(s) - setting it to 0, because Rapport removes this from the save in game hours")
		player.SetValue(0.0)
	EndIf
	If npc != None && npc.GetValue() != 0.0
		Rapport:Core.Trace("moisturizer: its NPC timer was " + npc.GetValue() + " minute(s) - setting it to 0")
		npc.SetValue(0.0)
	EndIf
EndFunction

;---------------------------------------------------------------------------
; The doorbell
;---------------------------------------------------------------------------

Event OnTimer(Int aiTimerID)
	If aiTimerID != kPollTimer
		Return
	EndIf

	; Scheduled first, for the same reason as the bridge: equipping and unequipping
	; armour on an actor is a good deal of engine work, and a stack that does not
	; come back must not take the poll with it.
	Self.StartTimer(Rapport:Core.PollSeconds(), kPollTimer)

	If _lib == None
		Self.Connect()
	Else
		Self.DrainOrders()
	EndIf
EndEvent

Function DrainOrders()
	; Bounded per poll for the same reason the bridge's drain is: a reload can
	; queue one order per standing mark at once, and equipping an armour piece is
	; a great deal more work for the engine than setting an overlay.
	Int budget = 4
	Int kind = Rapport:Core.TakeMoisturizerOrder()
	While kind != 0 && budget > 0
		Var[] args = new Var[5]
		args[0] = kind as Var
		args[1] = Rapport:Core.MoisturizerActorID() as Var
		args[2] = Rapport:Core.MoisturizerFront() as Var
		args[3] = Rapport:Core.MoisturizerOral() as Var
		args[4] = Rapport:Core.MoisturizerRear() as Var
		Self.CallFunctionNoWait("DoMoisturizerOrder", args)

		budget -= 1
		kind = Rapport:Core.TakeMoisturizerOrder()
	EndWhile
EndFunction

; Own stack: equipping armour and swapping headparts is a great deal of engine
; work, and the bridge learned the hard way what a stack that does not return
; costs when it is the one the poll is running on.
Function DoMoisturizerOrder(Int aiKind, Int aiFormID, Bool abFront, Bool abOral, Bool abRear)
	If _lib == None
		Return
	EndIf

	Actor target = Game.GetForm(aiFormID) as Actor
	If target == None
		Rapport:Core.DeferOrder(aiFormID)
		Rapport:Core.Trace("moisturizer: " + aiFormID + " no longer resolves - nothing was applied")
		Return
	EndIf

	If aiKind == 6
		; It refuses an actor whose 3D is not loaded, and says so only in its own
		; log. Rapport waits for the owner to be nearby before asking, so this
		; should be rare -- but taking the order off the queue is not the same as
		; carrying it out, so a miss is handed back rather than quietly dropped.
		If target.Is3DLoaded()
			; Once per layer. Its picker skips slots already in use and only wipes
			; the region when every slot is full, so these stack into a lot of cum
			; in one place rather than re-rolling the same decal.
			Int layers = Rapport:Core.MoisturizerLayers()
			Int n = 0
			While n < layers
				_lib.ApplyRandCumAtLocations(target, abFront, abOral, abRear, 0)
				n += 1
			EndWhile
			Rapport:Core.Trace("moisturizer: applied front=" + abFront + " oral=" + abOral + " rear=" + abRear + " x" + layers + " to " + aiFormID)
		Else
			Rapport:Core.DeferOrder(aiFormID)
			Rapport:Core.Trace("moisturizer: " + aiFormID + " is not loaded - deferred to the next tick")
		EndIf
	ElseIf aiKind == 7
		_lib.ClearAllCumFromActor(target)
		Rapport:Core.Trace("moisturizer: cleared everything from " + aiFormID)
	EndIf
EndFunction
