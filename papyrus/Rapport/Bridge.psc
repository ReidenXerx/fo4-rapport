Scriptname Rapport:Bridge extends Quest
{The only Papyrus in Rapport. AAF's API is Papyrus-only, so something has to live
 here; by the time a call arrives, everything that could be decided natively has
 been. This script starts scenes, watches AAF's events, and reports back.

 Nothing here polls, and no script is ever attached to an actor.

 Style note: the base sources are decompiled and so carry no default argument
 values. Every argument is passed explicitly. See docs/papyrus-toolchain.md.}

; ONE timer, and it is the only one this script has ever been able to keep.
;
; The poll ran 17 times and stopped at the exact poll that called StartTimer with
; a SECOND id, and the two places that used a second id both aborted at that
; statement -- the scene timer never fired, the stop timer never fired, and the
; poll never came back. So the clock for everything else lives on the plugin
; side, where it already lived for the watchdog, and Papyrus only ever asks.
Int Property kPollTimer = 1 AutoReadOnly

Struct Request
  Int id
  Actor first
  Actor second
  Int sceneID      ; AAF's own handle for the scene, learned from OnSceneInit
  Float duration   ; how long we asked for; nothing else will enforce it
EndStruct

AAF:AAF_API _api
Request[] _inFlight
Bool _ready = false
String _allMorphIDs = ""

;---------------------------------------------------------------------------
; Startup
;---------------------------------------------------------------------------

Event OnQuestInit()
	Self.Connect()
EndEvent

Event OnInit()
	Self.Connect()
EndEvent

; Runs on quest start AND on every game load, because the two halves of this mod
; have different lifetimes: this script's variables persist in the save, while
; Rapport.dll starts from nothing every launch. A script that remembered it had
; already connected left the freshly loaded plugin believing there was no bridge.
Function Connect()
	; No _connecting guard. It was a script variable, so it lived in the save, and
	; a game closed between setting it and clearing it left it true forever --
	; after which every Connect() returned immediately and silently, and the bridge
	; could never introduce itself again. The race it protected against (OnQuestInit
	; and OnInit both firing) only ever produced a duplicate log line, which is a
	; far smaller problem than a permanently deaf mod.
	;
	; Nothing below is unsafe to run twice: registering for the same event twice is
	; idempotent, and BridgeReady is just a report.

	; Anything still in here belongs to a session that is over: this array lives in
	; the save and the plugin does not. Dropping it without releasing the actors
	; first is exactly how an NPC ends up carrying AAF_ActorBusy for the rest of
	; the playthrough, unusable by every AAF mod on the machine.
	If _inFlight != None && _inFlight.Length > 0
		Rapport:Core.Trace("bridge: " + _inFlight.Length + " request(s) survived from a previous session - releasing their actors")
		Int stale = 0
		While stale < _inFlight.Length
			Self.ReleaseActor(_inFlight[stale].first)
			Self.ReleaseActor(_inFlight[stale].second)
			stale += 1
		EndWhile
	EndIf

	_inFlight = new Request[0]

	; GetAPI() tries AAF.esp before AAF.esm. Sex 'Em Up hardcodes only the .esm,
	; which is why it cannot see an AAF installed the other way round.
	_api = AAF:AAF_API.GetAPI()
	If _api == None
		Rapport:Core.Trace("bridge: AAF not found - no scene can be started")
		Rapport:Core.BridgeReady(false)
		Return
	EndIf

	; AAF_API re-broadcasts every event MainQuestScript sends, so these register on
	; the API object rather than on the quest behind it.
	;
	; The names are MANGLED on purpose, and this is the whole reason no AAF event
	; ever reached this script. Papyrus mangles a custom event name at COMPILE time
	; to "<declaring script, lowercased>_<Event>", and AAF's own pex therefore sends
	; "aaf:aaf_api_OnSceneInit". Sex 'Em Up writes the plain name in its source and
	; its pex still contains the mangled one, because it compiled against AAF's real
	; sources. Ours are decompiled, with the CustomEvent declarations put back by
	; hand -- enough for the handlers below to compile, not enough for the compiler
	; to mangle these. Proven by compiling both forms and reading the string table:
	; the plain name stays plain, and an explicit mangled name passes through. So we
	; write what AAF actually sends.
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnAAFReady")
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnWalkInit")
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnSceneInit")
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnSceneEnd")
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnAnimationStart")
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnAnimationStop")
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnAnimationQueryResult")

	_ready = true

	Self.ApplyDebugProfile()
	Self.ApplyTakeover()

	; Both of these must happen on every load, not only the first: the timer may
	; not have survived, and the plugin has no memory of the last session.
	; Cancel first. OnQuestInit and OnInit both call Connect, and the first live
	; run started three overlapping polls in the bridge because each one
	; started another timer. Papyrus cannot be asked whether a timer is already
	; running, and a remembered flag is the thing that has wedged this mod twice,
	; so cancelling unconditionally is the answer that needs no state at all.
	Self.CancelTimer(kPollTimer)
	Self.StartTimer(Rapport:Core.PollSeconds(), kPollTimer)
	Rapport:Core.Trace("bridge: connected to AAF " + _api.GetVersion() + " build " + _api.GetBuild())
	Rapport:Core.BridgeReady(true)
EndFunction

;---------------------------------------------------------------------------
; Called by the native scheduler once it has chosen a pair.
;---------------------------------------------------------------------------

; The doorbell. The scheduler never calls into the VM -- doing so crashed the
; game twice inside DispatchMethodCallImpl, because F4SE tasks run on a BSJobs
; job thread and the VM packs arguments through the per-thread scrap heap. So the
; bridge asks instead, on its own thread, and almost every ask returns nothing.
Event OnTimer(Int aiTimerID)
	If aiTimerID != kPollTimer
		Return
	EndIf

	; FIRST, before anything that can reach AAF.
	;
	; Every AAF API call ends in AAF_MainQuestScript.sendEvent, which is
	; ui.Invoke("HUDMenu", ...). The call delivers -- scenes start, overlays are
	; applied -- but the stack that made it does not come back. Two runs proved
	; it: seventeen polls and then the poll that called StartScene, and then the
	; same thing again with every StartTimer removed, which is what ruled the
	; timers out. They were the second casualty.
	;
	; Scheduling the next poll before the work means a stack that never returns
	; costs one poll rather than every poll after it.
	Self.StartTimer(Rapport:Core.PollSeconds(), kPollTimer)

	; Registrations do not survive a recompile, and _ready does survive the save,
	; so the script alone can end up permanently deaf: connected in its own memory,
	; registered for nothing. The plugin's fresh-every-session state is the only
	; reliable trigger for re-establishing them.
	If Rapport:Core.NeedsHandshake()
		Self.Connect()
	EndIf

	If _ready
		; What AAF says about itself, FIRST, so the watchdog decides on a number
		; taken this poll rather than on the last one it happened to see.
		;
		; -1 rather than 0 when there is no API to ask: "we could not ask" and
		; "AAF's quest is stopped" are different facts, and 0 is already spoken
		; for. Reporting a real status we did not read would be the worse bug --
		; it is what makes a broken framework look like a quiet one.
		Bool hudReady = UI.IsMenuOpen("HUDMenu")
		If _api == None
			Rapport:Core.NoteAAFStatus(-1, hudReady)
		Else
			Rapport:Core.NoteAAFStatus(_api.GetAAFStatus(), hudReady)
		EndIf

		Rapport:Core.Pump()

		; AAF does not end a scene when the duration it was given runs out, so we
		; do. The plugin holds the clock and answers with a request id or nothing.
		Int stopping = Rapport:Core.SceneToStop()
		If stopping != 0
			Int index = Self.FindRequest(stopping)
			If index >= 0
				Rapport:Core.Trace("bridge: request " + stopping + " has run its length - asking AAF to stop it")
				Self.StopSceneFor(_inFlight[index])
			EndIf
			Rapport:Core.NoteStopAsked()
		EndIf

		Int request = Rapport:Core.TakeRequest()
		If request != 0
			Self.BeginRequest(request, Rapport:Core.TakenFirstID(), Rapport:Core.TakenSecondID(), Rapport:Core.TakenDuration())
		EndIf

		Self.DrainOverlayOrders()
	EndIf
EndEvent

Function BeginRequest(Int aiRequest, Int aiFirstID, Int aiSecondID, Float afDuration)
	; Never assume the array exists. It lives in the save, and a save written
	; before this struct changed shape comes back as None rather than as an empty
	; array -- at which point Add fails, the function aborts halfway, and the
	; request simply never happens. Belt as well as braces: Connect() re-creates
	; it on every load now, but the cost of checking here is one comparison.
	If _inFlight == None
		Rapport:Core.Trace("bridge: the in-flight array came back None from the save - re-creating it")
		_inFlight = new Request[0]
	EndIf

	Actor akFirst = Game.GetForm(aiFirstID) as Actor
	Actor akSecond = Game.GetForm(aiSecondID) as Actor

	If !_ready || _api == None
		Rapport:Core.RequestFailed(aiRequest, "bridge not connected")
		Return
	EndIf

	If akFirst == None || akSecond == None || akFirst == akSecond
		Rapport:Core.RequestFailed(aiRequest, "invalid pair")
		Return
	EndIf

	; No SetActorLocked here. Locking before StartScene looked like prudent
	; reservation and was in fact a deadlock: the flag means "this actor is busy"
	; to AAF as well. AAF owns the actors for a scene it is running, and it walks
	; them there itself -- StartScene stamps them AAF_ActorBusy on our behalf.
	;
	; Which is exactly why an actor already carrying either flag must be left
	; alone: AAF will silently refuse a busy actor, and a request that died
	; without cleaning up leaves that flag on an NPC permanently. One NPC picked
	; by three failed runs became unusable for the rest of the save.
	If Self.IsOccupied(akFirst)
		Rapport:Core.NoteActorBusy(akFirst.GetFormID())
		Rapport:Core.RequestFailed(aiRequest, "the first actor is already busy in AAF")
		Return
	EndIf
	If Self.IsOccupied(akSecond)
		Rapport:Core.NoteActorBusy(akSecond.GetFormID())
		Rapport:Core.RequestFailed(aiRequest, "the second actor is already busy in AAF")
		Return
	EndIf

	; Their sexes, while we hold real Actors. The plugin decides from this which
	; one a scene's cum belongs to -- the receiving one, and only them.
	Rapport:Core.NoteActorSex(akFirst.GetFormID(), akFirst.GetLeveledActorBase().GetSex())
	Rapport:Core.NoteActorSex(akSecond.GetFormID(), akSecond.GetLeveledActorBase().GetSex())

	Request entry = new Request
	entry.id = aiRequest
	entry.first = akFirst
	entry.second = akSecond
	entry.sceneID = 0
	entry.duration = afDuration
	_inFlight.Add(entry, 1)

	; The AAF call goes on its OWN stack and this one returns immediately.
	;
	; A stack does not come back from StartScene. Measured three times, and the
	; schedule-the-timer-first attempt is what proved the stack is stuck rather
	; than merely failing: an errored stack would have let the already-scheduled
	; timer fire, and it did not. Papyrus will not start a second OnTimer while
	; the first is still running, so one stuck poll is every poll after it.
	;
	; OnSceneInit kept arriving throughout all three runs, so different handlers
	; DO run at the same time. It is re-entering the SAME handler that queues.
	; Putting the call in its own function is therefore enough.
	Var[] args = new Var[1]
	args[0] = aiRequest as Var
	Self.CallFunctionNoWait("DoStartScene", args)
EndFunction

; Runs on its own stack, courtesy of CallFunctionNoWait. Everything here may
; block forever without costing anything but this one stack.
Function DoStartScene(Int aiRequest)
	Int index = Self.FindRequest(aiRequest)
	If index < 0
		Rapport:Core.RequestFailed(aiRequest, "the request vanished before AAF was asked - nothing was started")
		Return
	EndIf
	If _api == None
		Rapport:Core.RequestFailed(aiRequest, "AAF went away before the scene could start")
		Return
	EndIf

	Actor akFirst = _inFlight[index].first
	Actor akSecond = _inFlight[index].second
	If akFirst == None || akSecond == None
		Return
	EndIf

	Actor[] actors = new Actor[2]
	actors[0] = akFirst
	actors[1] = akSecond

	AAF:AAF_API:SceneSettings settings = _api.GetSceneSettings()
	settings.duration = _inFlight[index].duration
	settings.usePackages = true          ; AAF walks them there; we do not fight its packages
	settings.skipWalk = false
	settings.isNPCControlled = true
	settings.preventFurniture = false
	settings.meta = "Rapport,autonomy"   ; so a scene of ours is identifiable as ours

	; AAF's readiness, CHECKED rather than merely logged.
	;
	; GetAAFStatus() is AAF_ReadyStatus + 1, and AAF_ReadyStatus becomes 1 when
	; AAF's DLL announces itself. So 2 means ready and anything less means it is
	; not listening yet. Every scene that has ever worked logged 2; the one that
	; vanished after three saves were loaded in a row logged 1, and StartScene
	; went into a framework that was not there. No error, no event, nothing --
	; just a request that never became a scene and a watchdog 210 seconds later.
	;
	; This number has been in the log since the first session and was never acted
	; on. Failing here costs one tick; the scheduler simply offers the pair again.
	Int status = _api.GetAAFStatus()
	If status < 2
		Rapport:Core.RequestFailed(aiRequest, "AAF is not ready (status " + status + ") - it has not announced itself since the last load")
		Self.ReleaseActor(akFirst)
		Self.ReleaseActor(akSecond)
		Return
	EndIf

	Rapport:Core.Trace("bridge: request " + aiRequest + " starting for " + akFirst.GetFormID() + " and " + akSecond.GetFormID() + ", aaf status " + status)
	_api.StartScene(actors, settings)
	Rapport:Core.Trace("bridge: StartScene returned for request " + aiRequest)
EndFunction

; Ends a scene AAF is running. -1 is AAF's own "all of it" -- the value its
; MainQuestScript uses when an actor walks out of range. One actor is enough;
; the scene is one thing, not one per participant.
Function StopSceneFor(Request akEntry)
	Var[] args = new Var[1]
	args[0] = akEntry.id as Var
	Self.CallFunctionNoWait("DoStopScene", args)
EndFunction

; Own stack, same reason as DoStartScene.
Function DoStopScene(Int aiRequest)
	Int index = Self.FindRequest(aiRequest)
	If index < 0 || _api == None
		Return
	EndIf

	If _inFlight[index].first != None
		_api.StopScene(_inFlight[index].first, -1)
	ElseIf _inFlight[index].second != None
		_api.StopScene(_inFlight[index].second, -1)
	EndIf
	Rapport:Core.Trace("bridge: StopScene returned for request " + aiRequest)
EndFunction

Function CancelRequest(Int aiRequest)
	Int index = Self.FindRequest(aiRequest)
	If index >= 0
		Self.Release(index, "cancelled")
	EndIf
EndFunction

;---------------------------------------------------------------------------
; AAF events
;---------------------------------------------------------------------------

Event AAF:AAF_API.OnAAFReady(AAF:AAF_API akSender, Var[] akArgs)
	; The per-load handshake. AAF initialises on every game load and our
	; registration for this survives in the save, so this is the one thing that
	; reliably happens after a load with no quest-start event to hang off.
	Rapport:Core.NoteEvent("OnAAFReady")
	Rapport:Core.Trace("aaf: ready")
	Self.Connect()
EndEvent

Event AAF:AAF_API.OnWalkInit(AAF:AAF_API akSender, Var[] akArgs)
	Self.TraceArgs("OnWalkInit", akArgs)
EndEvent

Event AAF:AAF_API.OnSceneInit(AAF:AAF_API akSender, Var[] akArgs)
	Self.TraceArgs("OnSceneInit", akArgs)

	; AAF reports FAILURES down this same event. A real scene init carries 11
	; arguments; a failure carries 4, with an error level in [0] and the message
	; in [1] -- "Failed to start 'FM' scene because there are no ... animations".
	;
	; Treating that as a scene start restarted the scenario, which asked for the
	; same impossible thing again, which failed again. A tight loop, and the log
	; said "scene started" every time round it.
	If akArgs == None || akArgs.Length < 11
		If akArgs != None && akArgs.Length > 1
			Rapport:Core.Trace("aaf REFUSED the scene: " + akArgs[1])
			Rapport:Core.SceneRefused(akArgs[1] as String)
		EndIf
		Return
	EndIf
	Int index = Self.FindRequestByActors(akArgs)
	If index >= 0
		; args[3] is AAF's scene id. Remembering it is what lets the end of this
		; scene be recognised as the end of THIS request rather than of whichever
		; one happened to be in flight.
		If akArgs.Length > 3
			Request entry = _inFlight[index]
			entry.sceneID = akArgs[3] as Int
			_inFlight[index] = entry
		EndIf
		; SceneStarted is what starts the plugin's clock for this scene -- here,
		; not when the request was made, because AAF walks the two of them across
		; a market first and that walk is not the scene.
		Rapport:Core.SceneStarted(_inFlight[index].id)
	EndIf
EndEvent

Event AAF:AAF_API.OnAnimationStart(AAF:AAF_API akSender, Var[] akArgs)
	Self.TraceArgs("OnAnimationStart", akArgs)

	; args[3] is the animation tag list, and it is the only thing that says what
	; this scene actually WAS. A scene plays several animations, so these are
	; accumulated on the plugin side and read once, when the scene ends.
	If akArgs != None && akArgs.Length > 3
		Int index = Self.FindRequestByActors(akArgs)
		If index >= 0
			Rapport:Core.NoteSceneTags(akArgs[3] as String)
		EndIf
	EndIf
EndEvent

; NOTE: the actor SLOT ORDER is not read.
;
; akArgs[1] is AAF's actor list in the order it placed them, and slot 0 is the
; receiving role in 559 of 562 two-actor animations -- it would settle a same-sex
; pair, which sex alone cannot. But it is a Var holding a packed array, Papyrus
; refuses "var as Var[]", and the function that unpacks it is an F4SE addition to
; Utility that is not in the vanilla Utility.pex. Declaring a native signature
; that cannot be verified fails at RUNTIME rather than at compile time, which is
; a worse trade than leaving a same-sex scene with nothing on either actor.
;
; So: mixed pairs are resolved by sex, same-sex pairs are left alone and said so
; in the log.

Event AAF:AAF_API.OnAnimationStop(AAF:AAF_API akSender, Var[] akArgs)
	Self.TraceArgs("OnAnimationStop", akArgs)
EndEvent

Event AAF:AAF_API.OnSceneEnd(AAF:AAF_API akSender, Var[] akArgs)
	Self.TraceArgs("OnSceneEnd", akArgs)
	Int index = Self.FindRequestByActors(akArgs)
	If index >= 0
		Self.Release(index, "")
	EndIf
EndEvent

Event AAF:AAF_API.OnAnimationQueryResult(AAF:AAF_API akSender, Var[] akArgs)
	; Not acted on yet. Logged so the argument layout is learned from the game:
	; AAF passes this one straight through from its DLL, so it is not readable
	; anywhere in its source.
	Self.TraceArgs("OnAnimationQueryResult", akArgs)
EndEvent

;---------------------------------------------------------------------------
; Housekeeping
;---------------------------------------------------------------------------

Bool Function IsOccupied(Actor akActor)
	If _api == None || akActor == None
		Return true
	EndIf
	If _api.AAF_ActorBusy != None && akActor.HasKeyword(_api.AAF_ActorBusy)
		Return true
	EndIf
	If _api.AAF_ActorLocked != None && akActor.HasKeyword(_api.AAF_ActorLocked)
		Return true
	EndIf
	Return false
EndFunction

; Undo what AAF stamped on our behalf. On a scene that ends properly AAF clears
; this itself; on one that never started, nothing would.
Function ReleaseActor(Actor akActor)
	If _api == None || akActor == None
		Return
	EndIf
	If _api.AAF_ActorBusy != None && akActor.HasKeyword(_api.AAF_ActorBusy)
		akActor.RemoveKeyword(_api.AAF_ActorBusy)
	EndIf
	_api.SetActorLocked(akActor, false)
EndFunction

; The debug hub's other half. AAF and MCM are both Papyrus-side, so the plugin
; reads debug.json and hands the table over; applying it has to happen here.
Function ApplyDebugProfile()
	Int count = Rapport:Core.DebugCount()
	If count <= 0
		Return
	EndIf

	Bool mcmReady = MCM.IsInstalled()
	Int applied = 0
	Int i = 0
	While i < count
		String target = Rapport:Core.DebugTarget(i)
		String settingKey = Rapport:Core.DebugKey(i)
		String value = Rapport:Core.DebugValue(i)

		If target == "aaf"
			If _api != None
				_api.ChangeSetting(settingKey, value)
				applied += 1
			EndIf
		ElseIf target == "mcm"
			If mcmReady
				String modName = Rapport:Core.DebugMod(i)
				String kind = Rapport:Core.DebugType(i)
				If kind == "bool"
					MCM.SetModSettingBool(modName, settingKey, value == "true")
				ElseIf kind == "int"
					MCM.SetModSettingInt(modName, settingKey, value as Int)
				ElseIf kind == "float"
					MCM.SetModSettingFloat(modName, settingKey, value as Float)
				Else
					MCM.SetModSettingString(modName, settingKey, value)
				EndIf
				applied += 1
			Else
				Rapport:Core.Trace("debug hub: MCM is not installed, so " + settingKey + " was not applied")
			EndIf
		EndIf

		i += 1
	EndWhile

	If mcmReady
		MCM.RefreshMenu()
	EndIf

	Rapport:Core.Trace("debug hub: applied " + applied + " of " + count + " setting(s)")
EndFunction

; Carries out what the plugin decided about overlays. Apply and remove both go
; through AAF rather than LooksMenu directly: AAF owns the set definitions, and
; it is the half that knows which overlays a set resolved to for this actor.
;
; Neither call reports anything back. AAF_API.ApplyOverlaySet sends an event to
; its own quest and returns immediately, so there is no success to check -- which
; is why the log says "asked" and not "applied".
Function DrainOverlayOrders()
	If _api == None
		Return
	EndIf

	; Bounded per poll on purpose. A reload can queue one order per standing
	; overlay at once, and the point of this framework is not to be the mod that
	; puts forty Papyrus calls in one frame.
	; Each order is handed to its own stack. The loop itself does no AAF work, so
	; it cannot be the thing that stops the poll.
	Int budget = 8
	Int kind = Rapport:Core.TakeOverlayOrder()
	While kind != 0 && budget > 0
		Var[] args = new Var[4]
		args[0] = kind as Var
		args[1] = Rapport:Core.OrderActorID() as Var
		args[2] = Rapport:Core.OrderSetID() as Var
		args[3] = Rapport:Core.OrderExtra() as Var
		Self.CallFunctionNoWait("DoOrder", args)

		budget -= 1
		kind = Rapport:Core.TakeOverlayOrder()
	EndWhile
EndFunction

; Own stack. One order, and nothing it can block matters to anybody else.
Function DoOrder(Int aiKind, Int aiFormID, String asSetID, String asExtra)
	; These two come BEFORE every guard below, deliberately. They are the orders
	; sent when AAF is broken, and every guard below asks whether AAF is working
	; -- including the one that returns early because form id 0 is not an actor.
	If aiKind == 9
		Self.ReviveAAF()
		Return
	ElseIf aiKind == 10
		Self.AskStartAAF()
		Return
	ElseIf aiKind == 11
		Self.RestartAAFQuest()
		Return
	EndIf

	If _api == None
		Return
	EndIf

	Actor target = Game.GetForm(aiFormID) as Actor
	If target == None
		Rapport:Core.DeferOrder(aiFormID)
		Rapport:Core.Trace("order: " + aiFormID + " no longer resolves - " + asSetID + " was not carried out")
		Return
	EndIf

	If aiKind == 1
		_api.ApplyOverlaySet(target, asSetID)
		Rapport:Core.Trace("aftermath: asked AAF for " + asSetID + " on " + aiFormID)
	ElseIf aiKind == 2
		_api.RemoveOverlaySet(target, asSetID)
		Rapport:Core.Trace("aftermath: asked AAF to remove " + asSetID + " from " + aiFormID)
	ElseIf aiKind == 3
		_api.ApplyMFGSet(target, asSetID)
		Rapport:Core.Trace("face: asked AAF for " + asSetID + " on " + aiFormID)
	ElseIf aiKind == 4
		Self.ReleaseActor(target)
		Rapport:Core.Trace("released the AAF busy keywords from " + aiFormID)
	ElseIf aiKind == 8
		; The next stage of a scenario. The stage names the KIND of moment it
		; wants and AAF chooses an animation that fits -- which is why a scenario
		; written once works with whatever packs somebody has, instead of only
		; with the pack it was written against.
		AAF:AAF_API:PositionSettings ps = _api.GetPositionSettings()
		ps.includeTags = asSetID
		ps.excludeTags = asExtra
		_api.ChangePosition(target, ps)
		Rapport:Core.Trace("stage: asked AAF for [" + asSetID + "] on " + aiFormID)
	ElseIf aiKind == 5
		; Both halves. The zeroed set puts every morph back to nothing; the block
		; removal is what lets go of them, because every expression Rapport
		; applies is locked and a morph left locked at zero is a face that can no
		; longer talk. No installed pack ships lock="false", so nothing here
		; demonstrates that applying zeros alone releases it -- and a frozen face
		; is exactly the failure this is meant to prevent.
		_api.ApplyMFGSet(target, asSetID)
		_api.RemoveMFGBlock(target, Self.AllMorphIDs())
		Rapport:Core.Trace("face: cleared " + asSetID + " from " + aiFormID)
	EndIf
EndFunction

; AAF's own per-load initialisation, run again.
;
; Not a workaround bolted onto AAF from outside: EveryTime_Initialization is what
; AAF runs on Actor.OnPlayerLoadGame, and what AAF runs AGAIN itself when the
; LooksMenu closes. It puts AAF_ReadyStatus back to 0, re-registers all
; twenty-four of its external event handlers, and ends with
;
;     ui.Invoke("HUDMenu", SWFPath + ".reboot", None)
;
; which is the part that goes missing. That call reaches into a Scaleform menu,
; and on a load screen -- or on the second of two loads in quick succession --
; there is no menu there to take it. AAF then sits at status 1 for the rest of
; the session: StartScene is accepted, no error comes back, no event arrives, and
; the scene simply never happens. Three saves loaded in a row produced exactly
; that, and the only trace was a number that had been in the log since the first
; session with nobody acting on it.
;
; Nothing is checked afterwards on purpose. The reboot is asynchronous -- AAF is
; ready when its SWF sends OnAAFReady back up, which is whenever the SWF gets
; there -- so reading the status here would only ever show the 0 this call just
; set. The watchdog sees the recovery on a later poll, and says so.
Function ReviveAAF()
	AAF:AAF_MainQuestScript mq = AAF:AAF_MainQuestScript.GetMainQuestScript()
	If mq == None
		Rapport:Core.Trace("aaf watchdog: AAF's main quest does not resolve - AAF is not installed")
		Return
	EndIf

	; The state BEFORE the call, because six attempts vanished without a trace and
	; "we asked" is not evidence that anything heard.
	;
	; The swf path is the decisive one. AAF's updater blanks it and only fills it
	; from its OnAAFReady handler, so while AAF is unready it is always empty and
	; EveryTime_Initialization takes its ui.Load branch rather than .reboot. If it
	; is STILL empty on the next attempt, that ui.Load never completed -- there was
	; no menu to load into. If it has filled, the interface loaded and simply never
	; announced itself, which is a different mod's bug and not ours to fix.
	Rapport:Core.Trace("aaf watchdog: before - quest running " + mq.IsRunning() + ", swf path \"" + mq.getSWFPath() + "\", HUDMenu open " + UI.IsMenuOpen("HUDMenu") + ", looking controls " + Game.IsLookingControlsEnabled())

	mq.EveryTime_Initialization()
	Rapport:Core.Trace("aaf watchdog: asked - AAF answers when its interface comes back up")
EndFunction

; AAF's own harder reboot, and the last thing tried.
;
; Stop() then Start() on the main quest is what AAF's updater does to itself when
; the version changes (AAF_UpdaterQuestScript.CheckVersion). It is heavier than
; re-running an init because a quest stop RESETS the script -- which is why
; GAME_DATA is carried across by hand here, exactly as AAF carries it. That string
; comes from setGameID and is the identity AAF's stored data is keyed to; losing
; it would leave AAF believing this is a different save.
Function RestartAAFQuest()
	AAF:AAF_MainQuestScript mq = AAF:AAF_MainQuestScript.GetMainQuestScript()
	If mq == None
		Rapport:Core.Trace("aaf watchdog: AAF's main quest does not resolve - AAF is not installed")
		Return
	EndIf

	String carried = mq.getGAME_DATA()
	Rapport:Core.Trace("aaf watchdog: the gentle restart did not take - stopping and starting AAF's main quest, carrying its game data \"" + carried + "\" across")

	; Back to back, with no wait between them. That is not an oversight: it is
	; exactly what AAF's own updater does on a version change, and the base source
	; reconstruction has no Utility.psc to wait with anyway.
	mq.Stop()
	mq.Start()

	; Put it back only if the restart cleared it. Writing over a value AAF has
	; already restored itself would be the one way this makes things worse.
	If mq.getGAME_DATA() == "" && carried != ""
		mq.saveGAME_DATA(carried)
		Rapport:Core.Trace("aaf watchdog: restored AAF's game data after the restart")
	EndIf

	Rapport:Core.Trace("aaf watchdog: AAF's main quest restarted, running " + mq.IsRunning() + ", game data \"" + mq.getGAME_DATA() + "\"")
EndFunction

; AAF's main quest is stopped, which is a different failure from a quiet one.
; Starting another mod's quest is a bigger act than re-running its init, and the
; reason it is stopped may be that AAF is on its way out of this save -- which
; nothing in this framework can see and the player can.
;
; Show() blocks the stack it is called on until the player answers. That is fine
; here and nowhere else: every order runs on its own stack via CallFunctionNoWait,
; so the poll is not waiting on this.
;
; No EveryTime_Initialization afterwards. Start() fires OnQuestInit, which runs
; AAF's own OneTime_Initialization, which calls it -- adding a second one would
; interleave two inits for no gain. If it does not take, the watchdog sees status
; 1 on a later poll and the ordinary restart handles it.
Function AskStartAAF()
	Message question = Game.GetFormFromFile(0x801, "Rapport.esp") as Message
	If question == None
		Rapport:Core.Trace("aaf watchdog: the question is missing from Rapport.esp - not asking")
		Return
	EndIf

	AAF:AAF_MainQuestScript mq = AAF:AAF_MainQuestScript.GetMainQuestScript()
	If mq == None
		Rapport:Core.Trace("aaf watchdog: AAF's main quest does not resolve - AAF is not installed")
		Return
	EndIf

	; Nine arguments because the decompiled base sources carry no default values.
	Int answer = question.Show(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
	If answer == 0
		Rapport:Core.NoteAAFRevivalChoice(true)
		mq.Start()
		Rapport:Core.Trace("aaf watchdog: started AAF's main quest at the player's word")
	Else
		Rapport:Core.NoteAAFRevivalChoice(false)
	EndIf
EndFunction

; Every morph id in the engine's facial table, as AAF wants them: one string of
; comma-separated numbers. Built once rather than written out, because a list of
; fifty literals is a list with a typo in it.
String Function AllMorphIDs()
	If _allMorphIDs == ""
		Int i = 0
		While i < 50
			If i == 0
				_allMorphIDs = "0"
			Else
				_allMorphIDs = _allMorphIDs + "," + i
			EndIf
			i += 1
		EndWhile
	EndIf
	Return _allMorphIDs
EndFunction

; Amendment A-11: Rapport configures the mods it works alongside, automatically
; but never secretly. Every quest below is checked before it is touched, named in
; the log with the reason, and started again when Rapport stops owning the
; feature. Nothing is deleted and no file of theirs is modified.
Function ApplyTakeover()
	Int count = Rapport:Core.TakeoverCount()
	If count <= 0
		Return
	EndIf

	Int i = 0
	While i < count
		Bool shouldStop = Rapport:Core.TakeoverShouldStop(i)
		Int formID = Rapport:Core.TakeoverFormID(i)
		Quest target = Game.GetForm(formID) as Quest
		String name = Rapport:Core.TakeoverName(i)

		If target == None
			Rapport:Core.Trace("takeover: " + name + " (" + formID + ") did not resolve - left alone")
		ElseIf shouldStop
			If target.IsRunning()
				target.Stop()
				Rapport:Core.Trace("takeover: STOPPED " + name + " - " + Rapport:Core.TakeoverReason(i))
			Else
				Rapport:Core.Trace("takeover: " + name + " was already stopped - left alone")
			EndIf
		Else
			If target.IsRunning()
				Rapport:Core.Trace("takeover: " + name + " is running again - nothing to restore")
			Else
				target.Start()
				Rapport:Core.Trace("takeover: STARTED " + name + " again - Rapport no longer owns that feature")
			EndIf
		EndIf

		i += 1
	EndWhile
EndFunction

Int Function FindRequest(Int aiRequest)
	Int i = 0
	While i < _inFlight.Length
		If _inFlight[i].id == aiRequest
			Return i
		EndIf
		i += 1
	EndWhile
	Return -1
EndFunction

Int Function FindRequestByActors(Var[] akArgs)
	; Prefer AAF's own scene id: every event of one scene carries it, so a match on
	; it is exact. Its position differs between events -- [3] on the init events,
	; [5] on the animation ones -- so any argument that equals a scene id we are
	; tracking counts, which survives a layout we have not seen yet.
	; Every argument is tested for TYPE before it is read as one.
	;
	; The previous version cast blindly -- akArgs[a] as Int -- and akArgs[1] is an
	; array of actor arrays. That cast fails with "Mismatched types", the failure
	; takes the surrounding expression with it, and the loop counter is assigned
	; None instead of being incremented. The loop then never advances.
	;
	; It span forever, on a stack that never ended, blocking every later OnTimer on
	; this script. Six debugging runs were spent on the symptom -- "the poll dies
	; when a scene starts" -- and the cause was 846,000 identical lines in the
	; Papyrus log the whole time.
	;
	; The timing that made it look like anything else: sceneID is 0 until
	; OnSceneInit sets it, so this inner loop never ran until the FIRST event after
	; a scene began. That event is OnAnimationStart, which is why the poll always
	; died exactly at scene start.
	Int i = 0
	While i < _inFlight.Length
		If _inFlight[i].sceneID != 0
			Int a = 0
			While a < akArgs.Length
				If akArgs[a] is Int
					If (akArgs[a] as Int) == _inFlight[i].sceneID
						Return i
					EndIf
				EndIf
				a += 1
			EndWhile
		EndIf
		i += 1
	EndWhile

	; Before OnSceneInit has told us the id there is nothing to match on, and while
	; MaxConcurrentScenes is 1 there is at most one candidate.
	If _inFlight.Length == 1
		Return 0
	EndIf
	Return -1
EndFunction

Function Release(Int aiIndex, String asWhy)
	Request entry = _inFlight[aiIndex]

	_inFlight.Remove(aiIndex, 1)

	If asWhy == ""
		Rapport:Core.SceneEnded(entry.id)
	Else
		; A request that failed leaves AAF's busy flag behind. Clearing it is the
		; difference between one wasted attempt and an NPC nobody can ever use.
		Self.ReleaseActor(entry.first)
		Self.ReleaseActor(entry.second)
		Rapport:Core.RequestFailed(entry.id, asWhy)
	EndIf
EndFunction

Function TraceArgs(String asEvent, Var[] akArgs)
	Rapport:Core.NoteEvent(asEvent)

	If akArgs == None
		Rapport:Core.Trace("aaf: " + asEvent + " (no args)")
		Return
	EndIf

	String line = "aaf: " + asEvent + " args[" + akArgs.Length + "]"
	Int i = 0
	While i < akArgs.Length && i < 16
		line = line + " [" + i + "]=" + akArgs[i]
		i += 1
	EndWhile
	Rapport:Core.Trace(line)
EndFunction
