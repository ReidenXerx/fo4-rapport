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
; Heads a watcher sweep turned to a scene (R-12), released when it ends.
Actor[] _lookers
Bool _ready = false
String _allMorphIDs = ""

;---------------------------------------------------------------------------
; Startup
;---------------------------------------------------------------------------

Event OnQuestInit()
	Self.Connect()
EndEvent

; Called by Rapport:Medic, through CallFunctionNoWait, when the plugin has not
; been polled for a while. It is the cheap half of the cure: if this script is
; healthy and only its clock went missing, one StartTimer is the entire fix. If
; the stack is stuck instead, this never runs at all -- which is not a failure,
; it is how the medic learns that the expensive cure is the one needed.
;
; Deliberately does NOT Connect() or touch AAF. Everything in here has to be safe
; to run at any moment, including in the middle of whatever else is going on.
Function ReArm()
	Rapport:Core.Trace("bridge: the medic re-armed the poll")
	Self.StartTimer(Rapport:Core.PollSeconds(), kPollTimer)
EndFunction

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

	; The tree's own progression. A scene started on a tree walks its authored
	; stages by itself, and these are how it says so -- without them the faces are
	; guessing from a clock while AAF is the one that knows.
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnAnimationChange")
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnStageEvent")
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnAnimationQueryResult")

	; TESTING. Nothing here acts on actor data; this exists to answer whether
	; GetActorData ever replies at all. Another mod's AAF addon calls it and
	; gets nothing back, with the same mangled name and with OnPositionData
	; answering beside it -- so the question is whether AAF answers ANYBODY.
	RegisterForCustomEvent(_api, "aaf:aaf_api_OnActorData")

	; From here we hear every scene AAF starts, so from here a busy flag with no
	; scene behind it can be told apart from one whose scene began before we were
	; listening. Connect runs more than once per load; restarting the clock each
	; time is correct, and it clears whatever we thought was running.
	Rapport:Core.NoteBridgeConnected()

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
			Rapport:Core.NoteAAFVersion(_api.GetVersion())
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

		; Before the drain, so a line a watcher wins is spoken on this same poll.
		Self.SweepWatchers()
		Self.DrainOverlayOrders()
	EndIf
EndEvent

; Who is near a running scene, and who can SEE it (R-12). Reports only: the
; plugin decides who is rolled, who speaks and what they say. HasDirectLOS
; is the reason this half lives here - the C++ side has no line-of-sight call.
;
; No AAF call anywhere in this function, so it cannot wedge the poll's stack.
Function SweepWatchers()
	Float radius = Rapport:Core.WatchRadius()
	If radius <= 0.0
		; No scene to watch (ended, or a new one inside its opening window):
		; every head this bridge turned is let go. Also what cleans up after a
		; save loaded mid-scene - the array lives in the save, the scene does not.
		Self.ReleaseLookers()
		Return
	EndIf
	Actor a = Game.GetForm(Rapport:Core.WatchFirstID()) as Actor
	Actor b = Game.GetForm(Rapport:Core.WatchSecondID()) as Actor
	If a == None || b == None || !a.Is3DLoaded()
		Return
	EndIf
	; ActorTypeNPC, Fallout4.esm 00013794 (79764), read from the master.
	Keyword npc = Game.GetFormFromFile(79764, "Fallout4.esm") as Keyword
	If npc == None
		Return
	EndIf
	ObjectReference[] near = a.FindAllReferencesWithKeyword(npc, radius)
	Actor player = Game.GetPlayer()
	Int i = 0
	While i < near.Length
		; The counter moves FIRST: a failed cast assigns None, and a loop whose
		; counter depends on a cast can spin forever (CLAUDE.md rule 3).
		Actor who = near[i] as Actor
		i += 1
		If who && who != a && who != b && who != player && !who.IsDead()
			; HasDirectLOS, not HasDetectionLOS: detection LOS is the STEALTH system,
			; and friendly townsfolk do not detect friendly NPCs - it answered no for a
			; whole scene to two watchers whose heads were turned to it. Facing is the
			; head turn's job; this is only the obstacle test.
			; EYE LEVEL TO THE BODY - measured, 2026-09-21. From the watcher's Head
			; node to a participant's Pelvis, or Head to Head. The empty-node ray runs
			; root to root, along the floor, and the furniture the pair lies on blocked
			; it on every sweep; head-to-body found McDonough's gap past a wall on 3 of 5.
			; Walls still block it, which is why hearing (the plugin's side) exists.
			Bool sees = who.HasDirectLOS(a, "Head", "Pelvis") || who.HasDirectLOS(b, "Head", "Pelvis")
			If !sees
				sees = who.HasDirectLOS(a, "Head", "Head") || who.HasDirectLOS(b, "Head", "Head")
			EndIf
			If Rapport:Core.NoteWatcher(who.GetFormID(), sees)
				; They noticed: the HEAD turns (pathing False = no walking over).
				; Two arguments - the decompiled base carries no defaults.
				who.SetLookAt(a, False)
				If _lookers == None
					_lookers = new Actor[0]
				EndIf
				_lookers.Add(who)
			EndIf
		EndIf
	EndWhile
	Rapport:Core.EndWatchSweep()
EndFunction

; Let go of every head a sweep turned. Never assume the array: it lives in the
; save, and a save from before it existed hands back None (CLAUDE.md rule 4).
Function ReleaseLookers()
	If _lookers == None || _lookers.Length == 0
		Return
	EndIf
	Int i = 0
	While i < _lookers.Length
		Actor who = _lookers[i]
		i += 1
		If who
			who.ClearLookAt()
		EndIf
	EndWhile
	Rapport:Core.Trace("watchers: released " + _lookers.Length + " head(s)")
	_lookers.Clear()
EndFunction

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
	; And the engine's own relationship between them, while we hold real Actors -
	; imported into Rapport's store the first time this pair interacts (R-2, R-5).
	Rapport:Core.NoteVanillaRelationship(akFirst.GetFormID(), akSecond.GetFormID(), Rapport:Relations.RankBetween(akFirst, akSecond), Rapport:Relations.AreBloodRelated(akFirst, akSecond), Rapport:Relations.ArePartners(akFirst, akSecond))

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
; The pair in the order AAF should SLOT them. For a male + female pair the
; female goes first, always (owner, 2026-09-21).
;
; AAF slots actors in array order. For a GENDERED position (F_M) it sorts them
; itself, which is why this went unnoticed; for a gender-neutral one ("2P",
; "NULLTOSELF") slot 0 simply goes to whoever is first - and slot 0 is the
; receiving role in 559 of the 562 two-actor animations that name both
; genders. The first real miss: the guard listed first, "DR Fist Anal 02"
; chosen, and a male receiver moaning with the receiver's sounds.
;
; Same-sex pairs keep the caller's order: there is no convention to follow.
; Used by EVERY array handed to AAF, so a query and a start never disagree
; about who is who.
Actor[] Function ForAAF(Actor akFirst, Actor akSecond)
	Actor[] actors = new Actor[2]
	actors[0] = akFirst
	actors[1] = akSecond
	; GetSex: 0 male, 1 female, -1 none. Swap only on a clear male-first pair.
	If akFirst.GetLeveledActorBase().GetSex() == 0 && akSecond.GetLeveledActorBase().GetSex() == 1
		actors[0] = akSecond
		actors[1] = akFirst
		Rapport:Core.Trace("bridge: female first for AAF's slots - " + Rapport:Core.FormIdText(akSecond.GetFormID()) + " takes slot 0")
	EndIf
	Return actors
EndFunction

Function DoStartScene(Int aiRequest)
	Int index = Self.FindRequest(aiRequest)
	If index < 0
		Rapport:Core.RequestFailed(aiRequest, "the request vanished before AAF was asked - nothing was started")
		Return
	EndIf
	; EVERY failure below here must Release, not just Return.
	;
	; BeginRequest already added this request to _inFlight, and Release is the only
	; thing that ever removes one -- reachable from OnSceneEnd alone, for a scene
	; AAF actually ends. A request abandoned here therefore stays in the array
	; forever, and one stranded entry is not a leak of one scene: FindRequestByActors
	; matches on AAF's scene id, every stranded entry has none, and its
	; single-candidate fallback stops working the moment the array holds two. From
	; then on EVERY later scene is unmatchable -- never started, never stopped,
	; never recorded, both actors left flagged busy -- and each one strands its own
	; entry, so it compounds. Only Connect() re-creating the array recovers it,
	; which means a game load.
	If _api == None
		Self.Release(index, "AAF went away before the scene could start")
		Return
	EndIf

	Actor akFirst = _inFlight[index].first
	Actor akSecond = _inFlight[index].second
	If akFirst == None || akSecond == None
		Self.Release(index, "an actor went away before the scene could start")
		Return
	EndIf

	Actor[] actors = Self.ForAAF(akFirst, akSecond)

	AAF:AAF_API:SceneSettings settings = _api.GetSceneSettings()
	settings.duration = _inFlight[index].duration
	settings.usePackages = true          ; AAF walks them there; we do not fight its packages
	settings.skipWalk = false
	settings.isNPCControlled = true
	settings.preventFurniture = false
	settings.meta = "Rapport,autonomy"   ; so a scene of ours is identifiable as ours

	; DO NOT set startEquipmentSet or stopEquipmentSet here, and do not call
	; ApplyEquipmentSet during a scene. Setting a start set REPLACES AAF's own
	; undressing and both actors stay fully dressed, with nothing in any log to say
	; so. See docs/aaf-under-the-hood.md finding 20.

	; The tree this scene will run, chosen from the catalogue now that the plugin
	; knows both sexes. THIS is the only moment a position can be chosen: AAF's
	; ChangePosition refuses everything -- tags, a named position, even no filters
	; at all -- while StartScene honours a position id without complaint.
	;
	; Empty means nothing in the catalogue reaches a climax for this pair, and the
	; scene starts unconstrained rather than not at all. The plugin says so when
	; that happens; it is not silent.
	String chosen = Rapport:Core.ScenePosition()
	If chosen != ""
		settings.position = chosen
	EndIf

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
		; Release, not a bare Return: this is the failure that actually happens --
		; the watchdog's whole reason for existing -- so it is the one most likely
		; to strand an entry and blind every later scene.
		;
		; Release's failure branch reports RequestFailed and clears both actors'
		; busy flags itself, which is what the two hand-rolled ReleaseActor calls
		; used to do here. One of them was also an AAF call on this stack, and a
		; stack that calls AAF often does not come back -- so the second actor was
		; stranded whenever the first wedged.
		Self.Release(index, "AAF is not ready (status " + status + ") - it has not announced itself since the last load")
		Return
	EndIf

	Rapport:Core.Trace("bridge: request " + aiRequest + " starting for " + Rapport:Core.FormIdText(akFirst.GetFormID()) + " and " + Rapport:Core.FormIdText(akSecond.GetFormID()) + ", aaf status " + status)
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

			; And STOP WAITING FOR IT, if it was ours.
			;
			; args[3] on the failure form carries the meta tag the scene was
			; requested with -- the same string set in Start() -- so a refusal of
			; our scene is told from a refusal of somebody else's by the thing AAF
			; already puts in the message. Without this the request stayed in
			; flight until the 780-second watchdog: thirteen minutes of both actors
			; flagged busy in AAF and every tick answering "a scene is already
			; running", which looks exactly like a wedge and was taken for one.
			If akArgs.Length > 3 && (akArgs[3] as String) == "Rapport,autonomy"
				If Rapport:Core.RefusedOurScene("AAF refused the scene: " + akArgs[1])
					Rapport:Core.Trace("bridge: that refusal was ours - the request is failed rather than left to time out")
				EndIf
			EndIf
		EndIf
		Return
	EndIf
	; EVERY scene, not only ours. We track the SCENE, not its actors: args[1] holds
	; the actors but it is an array of arrays, and Papyrus cannot cast a Var to a
	; Var[] -- "cannot cast a var to a var[], types are incompatible". The only way
	; to read it is the implicit string rendering TraceArgs relies on, and scraping
	; a debug rendering for form ids is not evidence worth building on.
	;
	; The id alone is enough for a CONSERVATIVE rule: a busy flag is only ever
	; called stale when NO scene is running anywhere. If some other mod has a scene
	; up we simply decline to judge, which is the safe direction to be wrong in.
	If akArgs[3] is Int
		Rapport:Core.NoteSceneLive(akArgs[3] as Int)
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
			; args[2] BEFORE args[3]: the tag handler consults the position, both as an
			; override key and as words to read when the tags name no act.
			Rapport:Core.NoteScenePosition(akArgs[2] as String)
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

	; Whoever's scene it was, its actors are free now. The id's position differs
	; between events, so every Int is offered -- the plugin ignores 0 and anything
	; that matches no scene it is tracking, exactly as FindRequestByActors does.
	Int e = 0
	While e < akArgs.Length
		If akArgs[e] is Int
			Rapport:Core.NoteSceneEnded(akArgs[e] as Int)
		EndIf
		e += 1
	EndWhile

	Int index = Self.FindRequestByActors(akArgs)
	If index >= 0
		Self.Release(index, "")
	EndIf
EndEvent

Event AAF:AAF_API.OnAnimationChange(AAF:AAF_API akSender, Var[] akArgs)
	; The tree moved to its next position, and THIS is the event that says what
	; the scene is doing now. The layout has been observed: args[2] is the
	; position name, args[3] the tag list -- same shape as OnAnimationStart.
	Self.TraceArgs("OnAnimationChange", akArgs)

	; OnAnimationStart fires ONCE, on the tree's entry animation. Measured: two
	; scenes produced 2 Start events and 10 Change events. So forwarding only
	; Start meant the plugin's idea of what the scene was doing froze on the
	; opening position and never advanced -- for the whole rest of the tree.
	;
	; That is not cosmetic. The aftermath puts the cum in the last hole touched,
	; and it was reading the FIRST one. Five trees on this install end in a
	; different region from the one they open in -- "Gay Pit Doggy" enters rear
	; and ends oral, and three others open on a position that names no act at
	; all, so nothing was applied where something plainly should have been.
	If akArgs != None && akArgs.Length > 3
		Int index = Self.FindRequestByActors(akArgs)
		If index >= 0
			; args[2] BEFORE args[3]: the tag handler consults the position, both as an
			; override key and as words to read when the tags name no act.
			Rapport:Core.NoteScenePosition(akArgs[2] as String)
			Rapport:Core.NoteSceneTags(akArgs[3] as String)
		EndIf
	EndIf
EndEvent

Event AAF:AAF_API.OnStageEvent(AAF:AAF_API akSender, Var[] akArgs)
	Self.TraceArgs("OnStageEvent", akArgs)
EndEvent

Event AAF:AAF_API.OnAnimationQueryResult(AAF:AAF_API akSender, Var[] akArgs)
	; Not acted on yet. Logged so the argument layout is learned from the game:
	; AAF passes this one straight through from its DLL, so it is not readable
	; anywhere in its source.
	Self.TraceArgs("OnAnimationQueryResult", akArgs)
EndEvent

Event AAF:AAF_API.OnActorData(AAF:AAF_API akSender, Var[] akArgs)
	; TESTING ONLY. Documented to carry the actor, a status string from
	; $WALKING/$ANIMATING/$LOCKING/$UNLOCKING/$LOCKED/$UNLOCKED/$WAITING, and
	; that actor's stat names and values as two parallel arrays.
	Self.TraceArgs("OnActorData", akArgs)
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
; Through AAF's own unlock ONLY. The busy keyword is AAF's: it removes it at unlock,
; and its interface refuses a new scene for an actor still unlocking whatever the
; keyword says - so stripping it ourselves gains nothing and can double-book an
; actor (AAF's author, fo4-rapport issue #1, §7). Every caller already knows no
; scene of this actor is running: a refusal (AAF has already cleared it), a request
; left over from another world, or a flag nobody's live scene explains.
; See DoOrder, kind 33, and src/Morphs.h. Both sexes' maps: LooksMenu keeps morphs
; per sex, and which one AAF wrote into is not ours to assume.
;
; An actor AAF has in a scene RIGHT NOW is left alone -- that morph is AAF's live
; state, and taking it off mid-scene is breaking somebody else's scene. The busy
; keyword is read as a Papyrus PROPERTY of AAF_API, which is not an AAF call.
Function ClearAAFMorphs(Int aiFormID)
	Actor who = Game.GetForm(aiFormID) as Actor
	If who == None
		Return
	EndIf
	Keyword aafMorphs = Game.GetFormFromFile(0x00000F9E, "AAF.esm") as Keyword
	If aafMorphs == None
		Rapport:Core.Trace("morphs: AAF_MorphKeyword (AAF.esm 000F9E) did not resolve - nothing cleared")
		Return
	EndIf
	If _api != None && who.HasKeyword(_api.AAF_ActorBusy)
		Rapport:Core.Trace("morphs: " + Rapport:Core.FormIdText(aiFormID) + " is in an AAF scene right now - left alone")
		Return
	EndIf
	BodyGen.RemoveMorphsByKeyword(who, True, aafMorphs)
	BodyGen.RemoveMorphsByKeyword(who, False, aafMorphs)
	If who.Is3DLoaded()
		BodyGen.UpdateMorphs(who)
	EndIf
	Rapport:Core.Trace("morphs: AAF's keyed morphs cleared from " + Rapport:Core.FormIdText(aiFormID))
EndFunction

Function ReleaseActor(Actor akActor)
	If _api == None || akActor == None
		Return
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

	; NOTHING DRAINS DURING A LOADING SCREEN. Every order below ends in an AAF call,
	; and every AAF call is a ui.Invoke into AAF's Flash interface - which the
	; loading screen has torn down. 2026-09-21: a scene ended at 20:20:05, the
	; player fast travelled at 20:20:24, and at 20:20:27 - the moment the
	; aftermath's heat-overlay removal came due, 22s after the scene as it always
	; does - the game died in Scaleform (GFx::AS3::Traits::GetVT) reading freed
	; memory. Orders are not dropped: they wait in the queue for the first poll
	; after the load, which is exactly when they can be carried out.
	If UI.IsMenuOpen("LoadingMenu")
		Int waiting = Rapport:Core.PendingOrders()
		If waiting > 0
			Rapport:Core.Trace("orders: loading screen up - " + waiting + " order(s) wait for it to finish")
		EndIf
		Return
	EndIf

	; Bounded per poll on purpose. A reload can queue one order per standing
	; overlay at once, and the point of this framework is not to be the mod that
	; puts forty Papyrus calls in one frame.
	; Each order is handed to its own stack. The loop itself does no AAF work, so
	; it cannot be the thing that stops the poll.
	;
	; THE BUDGET IS TESTED BEFORE THE POP, and that is the whole shape of this
	; loop. TakeOverlayOrder DESTROYS the order it returns -- it pops the front of
	; the native deque -- so popping at the bottom of the body and then failing the
	; budget test threw one order away on every over-budget poll, silently, with no
	; log line and no way to get it back.
	;
	; Every kind that travels this queue is issued once and forgotten in the same
	; breath: Aftermath erases the mark as it queues the removal, Expressions
	; clears the wearing list as it queues the clear. So a dropped order is not a
	; delayed order, it is permanent damage -- an overlay nothing will ever take
	; off, a face that can no longer talk, an NPC carrying AAF_ActorBusy for the
	; rest of the save, or the one question about restarting AAF that is never
	; asked. And more than eight at once is the DESIGNED case, not an edge: a
	; reload queues one per standing mark.
	Int budget = 8
	While budget > 0
		Int kind = Rapport:Core.TakeOverlayOrder()
		If kind == 0
			Return
		EndIf

		Var[] args = new Var[5]
		args[0] = kind as Var
		args[1] = Rapport:Core.OrderActorID() as Var
		args[2] = Rapport:Core.OrderSetID() as Var
		args[3] = Rapport:Core.OrderExtra() as Var
		args[4] = Rapport:Core.OrderVoice() as Var
		Self.CallFunctionNoWait("DoOrder", args)

		budget -= 1
	EndWhile

	; Whatever is left waits for the next poll rather than being eaten by this one.
	Int waiting = Rapport:Core.PendingOrders()
	If waiting > 0
		Rapport:Core.Trace("orders: " + waiting + " still queued after this poll's budget of 8 - they wait, they are not dropped")
	EndIf
EndFunction

; Own stack. One order, and nothing it can block matters to anybody else.
Function DoOrder(Int aiKind, Int aiFormID, String asSetID, String asExtra, Int aiVoice)
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

	; AAF's own scene morphs left on somebody (Erection, CErection, ...). ALSO above
	; the AAF guards: it makes no AAF call -- BodyGen is LooksMenu's and returns --
	; and the load sweep sends it for actors who are not loaded, which the guards
	; below would refuse. Cleared BY KEYWORD, so only AAF's layer goes: BodyGen's
	; bodies and the player's own LooksMenu sliders sit under other keys.
	If aiKind == 33
		Self.ClearAAFMorphs(aiFormID)
		Return
	EndIf

	; The teleports, ABOVE the AAF guards and above the _api check.
	;
	; They are test scaffolding, they do not touch AAF, and the actor being
	; unloaded is the whole reason to send one -- so every guard below, all of
	; which exist to keep AAF calls away from absent actors, would refuse exactly
	; the case these are for.
	If aiKind == 18
		; setID is the pitch, extra is the yaw, both in degrees and both already
		; worked out by the plugin -- it has the positions and real trigonometry.
		;
		; TWO FIELDS, not one comma-separated string: splitting that string needs
		; StringUtil, which is SKSE and does not exist in Fallout 4. An Order
		; already carries two strings, so there was never a reason to pack them.
		;
		; SetAngle takes (pitch, roll, yaw). Roll stays 0 -- a rolled camera is a
		; bug, never a request.
		; Game.SetCameraTarget -- the function the engine provides for exactly this,
		; found by READING THE BASE SOURCES instead of guessing.
		;
		; The first version computed a heading by hand and called SetAngle on the
		; PLAYER. That crashed the game: 2026-09-20 02:59:52 it ran with pitch
		; -0.13 yaw 104.63, and a second later the game died on a null
		; function-pointer call with GameVM::ProcessEvent(PositionPlayerEvent&),
		; BSTEventSource<PositionPlayerEvent>::Notify, ForceFullUpdate and
		; DispatchRenderSafeCalls on the stack. Rotating the player raises
		; PositionPlayerEvent and the engine does a full reposition off the back of
		; it -- which also explains the loading screen that appeared from a command
		; that had explicitly not teleported anybody.
		;
		; SetCameraTarget touches the camera and not the player's body. There was
		; never a need for trigonometry here; there was a need to read the API.
		If asSetID == "off"
			; StopDialogueCamera(abConsiderResume, abSwitchingTo1stP). Both passed
			; explicitly; ForceFirstPerson after it is the guaranteed restore.
			Game.StopDialogueCamera(False, True)
			Game.ForceFirstPerson()
			Rapport:Core.Trace("look: camera released back to the player")
		Else
			Actor lookAt = Game.GetForm(aiFormID) as Actor
			If lookAt == None
				Rapport:Core.Trace("look: " + Rapport:Core.FormIdText(aiFormID) + " does not resolve")
				Return
			EndIf
			; StartDialogueCameraOrCenterOnTarget, not SetCameraTarget.
			;
			; SetCameraTarget was tried in game and measured: it forces third
			; person and IGNORES the actor passed to it -- pointing it at two
			; different actors produced byte-identical frames. This one takes an
			; ObjectReference and its name says centre-on-target, which is the
			; operation wanted. StopDialogueCamera is its documented exit.
			Game.StartDialogueCameraOrCenterOnTarget(lookAt as ObjectReference)
			Rapport:Core.Trace("look: centred the camera on " + Rapport:Core.FormIdText(aiFormID) 				+ " (heading " + (Game.GetPlayer().GetHeadingAngle(lookAt as ObjectReference) as Int) + "deg)")
		EndIf
		Return
	EndIf

	If aiKind == 24
		Actor dest = Game.GetForm(aiFormID) as Actor
		If dest == None
			Rapport:Core.Trace("travel: " + Rapport:Core.FormIdText(aiFormID) + " does not resolve")
			Return
		EndIf
		; FastTravel takes an ObjectReference, and an actor IS one -- so we can
		; travel to a person rather than needing a map marker's form id.
		Game.FastTravel(dest as ObjectReference)
		Rapport:Core.Trace("travel: fast travelling to " + Rapport:Core.FormIdText(aiFormID))
		Return
	EndIf

	If aiKind == 23
		Rapport:Core.Trace("quit: closing the game at the mailbox's request")
		Debug.QuitGame()
		Return
	EndIf

	If aiKind == 22
		Debug.SetGodMode(asSetID == "1")
		If asSetID == "1"
			Rapport:Core.Trace("god: god mode ON")
		Else
			Rapport:Core.Trace("god: god mode off")
		EndIf
		Return
	EndIf

	If aiKind == 21
		Actor subject = Game.GetForm(aiFormID) as Actor
		If subject == None
			Rapport:Core.Trace("state: " + Rapport:Core.FormIdText(aiFormID) + " does not resolve")
			Return
		EndIf
		Actor pc = Game.GetPlayer()

		; Every argument passed explicitly -- the base sources are decompiled and
		; carry no defaults, so an omitted one is a compile error rather than a
		; silent zero. getDistance is lowercase in ObjectReference.psc; that is
		; not a typo here.
		String line = "state: " + Rapport:Core.FormIdText(aiFormID)
		line = line + " loaded=" + subject.Is3DLoaded()
		line = line + " scene=" + subject.IsInScene()
		line = line + " combat=" + subject.IsInCombat()
		line = line + " talking=" + subject.IsTalking()
		line = line + " weapon=" + subject.IsWeaponDrawn()
		line = line + " sneaking=" + subject.IsSneaking()
		line = line + " dead=" + subject.IsDead()
		line = line + " unconscious=" + subject.IsUnconscious()
		line = line + " sit=" + subject.GetSitState()
		line = line + " sleep=" + subject.GetSleepState()
		line = line + " relationship=" + subject.GetRelationshipRank(pc)
		line = line + " dist=" + (subject.getDistance(pc) as Int)
		line = line + " heading=" + (pc.GetHeadingAngle(subject) as Int) + "deg"
		Rapport:Core.Trace(line)
		Return
	EndIf

	If aiKind == 20
		Actor frozen = Game.GetForm(aiFormID) as Actor
		If frozen == None
			Rapport:Core.Trace("ai: " + Rapport:Core.FormIdText(aiFormID) + " does not resolve")
			Return
		EndIf
		; EnableAI(abEnable, abPauseVoice) -- TWO arguments. The decompiled base
		; sources carry no default values, so every one must be passed; leaving
		; the second off is a compile error, not a silent default.
		frozen.EnableAI(asSetID == "1", False)
		If asSetID == "1"
			Rapport:Core.Trace("ai: " + Rapport:Core.FormIdText(aiFormID) + " is thawed")
		Else
			Rapport:Core.Trace("ai: " + Rapport:Core.FormIdText(aiFormID) + " is frozen where they stand")
		EndIf
		Return
	EndIf

	If aiKind == 19
		; DISABLED. PassTime(1) ADVANCED ABOUT 100 GAME HOURS.
		;
		; Measured 2026-09-20 03:42 on the owner's save: hour 781.54 before,
		; 881.55 after a single PassTime(1) -- four game days, from a call whose
		; parameter is literally named aiHours. Confirmed twice, and corroborated
		; by Rapport's own ledger clock (781.3) which is an entirely separate code
		; path.
		;
		; Whatever the argument means, it is not "hours to advance". It was shipped
		; on the strength of a parameter NAME, in the same session as a document
		; arguing that names mislead -- the survey gave a signature and that was
		; mistaken for knowing what the function does.
		;
		; Left reachable but inert so the next attempt reads this first. If it is
		; ever re-enabled: test on a THROWAWAY save, read the clock before and
		; after, and start from the smallest possible value.
		Rapport:Core.Trace("passtime: REFUSED - PassTime(1) advanced ~100 game hours on 2026-09-20; see Bridge.psc")
		Return
	EndIf

	If aiKind == 99
		; DISABLED. SetAngle ON THE PLAYER CRASHED THE GAME.
		;
		; 2026-09-20 02:59:52 this ran with pitch -0.13 yaw 104.63; at 02:59:53 the
		; game died on a null function-pointer call, and the stack was the player
		; repositioning path -- GameVM::ProcessEvent(PositionPlayerEvent&),
		; BSTEventSource<PositionPlayerEvent>::Notify, ForceFullUpdate,
		; DispatchRenderSafeCalls, with PlayerCharacter and "Diamond City" among
		; the relevant objects. A loading screen appeared first, from a command
		; that had explicitly NOT teleported anybody, which is the same event
		; firing.
		;
		; So rotating the player is not the harmless camera nudge it looks like:
		; it raises PositionPlayerEvent and the engine does a full reposition off
		; the back of it. Whatever the right way to aim the camera is, this is not
		; it, and a crash is far too high a price for a nicer screenshot.
		;
		; Left in place rather than deleted so the next attempt starts from the
		; finding instead of rediscovering it.
		Rapport:Core.Trace("look: REFUSED - SetAngle on the player crashed the game on 2026-09-20; see Bridge.psc")
		Return
	EndIf

	If aiKind == 16 || aiKind == 17
		Actor who = Game.GetForm(aiFormID) as Actor
		If who == None
			Rapport:Core.Trace("move: " + Rapport:Core.FormIdText(aiFormID) + " does not resolve")
			Return
		EndIf
		If aiKind == 16
			; setID and extra are an optional X/Y offset in world units. Empty
			; means zero, which is "land on top of them" -- fine for reaching
			; somebody, useless for LOOKING at them, which is what the offset is
			; for: you cannot see a face from inside it.
			Game.GetPlayer().MoveTo(who, asSetID as Float, asExtra as Float, 0.0, False)
			; And land somewhere walkable. An offset move puts the player wherever
			; the arithmetic said, which is regularly inside a wall; the engine
			; has a function for exactly this and it was never being called.
			Game.GetPlayer().MoveToNearestNavmeshLocation()
			Rapport:Core.Trace("move: put the player next to " + Rapport:Core.FormIdText(aiFormID) 				+ " (offset " + asSetID + "," + asExtra + ")")
		Else
			who.MoveTo(Game.GetPlayer(), 0.0, 0.0, 0.0, True)
			Rapport:Core.Trace("move: brought " + Rapport:Core.FormIdText(aiFormID) + " to the player")
		EndIf
		Return
	EndIf

	If aiKind == 27
		; The Narrator: headline then numbers, top-left, fading on their own - never a
		; box to click. One stack, so the two lines cannot swap.
		Debug.Notification(asSetID)
		If asExtra != ""
			Debug.Notification(asExtra)
		EndIf
		Return
	EndIf

	If aiKind == 26
		Actor a = Game.GetForm(aiFormID) as Actor
		Actor b = Game.GetForm(asSetID as Int) as Actor
		If a == None || b == None
			Rapport:Core.Trace("relation: an id does not resolve to an actor")
			Return
		EndIf
		Rapport:Core.Trace("relation: " + Rapport:Core.FormIdText(aiFormID) + " -> " + Rapport:Core.FormIdText(b.GetFormID()) + " rank " + a.GetRelationshipRank(b) + " | back " + b.GetRelationshipRank(a) + " | family " + a.HasFamilyRelationship(b) + " | blood " + Rapport:Relations.AreBloodRelated(a, b) + " | partner " + Rapport:Relations.ArePartners(a, b) + " | stored " + Rapport:Core.IsPairSeeded(aiFormID, b.GetFormID()) + " | bondBetween " + Rapport:Relations.BondBetween(a, b) + " | partnered " + Rapport:Relations.HasPartner(a) + "/" + Rapport:Relations.HasPartner(b) + " | faithful " + Rapport:Core.FaithfulnessOf(aiFormID) + "/" + Rapport:Core.FaithfulnessOf(b.GetFormID()) + " | affair " + Rapport:Core.IsAffairPair(aiFormID, b.GetFormID()))
		Return
	EndIf

	If aiKind == 25
		; A scene bark (R-9). Above the _api check on purpose: Say is not an AAF
		; call, and a bark must not go quiet because AAF's API reference did.
		;
		; GetFormFromFile, never GetForm. setID is the Topic's FILE-RELATIVE id
		; and the load-order byte is added back here, because Rapport.esp sits at
		; a different index on every machine.
		Actor speaker = Game.GetForm(aiFormID) as Actor
		Topic line = Game.GetFormFromFile(asSetID as Int, "Rapport.esp") as Topic
		If speaker == None || line == None
			Rapport:Core.Trace("bark: speaker " + Rapport:Core.FormIdText(aiFormID) + " or topic " + asSetID + " does not resolve - not said")
			Return
		EndIf
		; Dropped, not deferred. A line is about THIS moment of the scene, and
		; saying it when they come back into the cell would be about nothing.
		If !speaker.Is3DLoaded()
			Rapport:Core.Trace("bark: " + Rapport:Core.FormIdText(aiFormID) + " is not loaded - line dropped")
			Return
		EndIf
		ObjectReference listener = Game.GetForm(asExtra as Int) as ObjectReference
		; A voice we did not render borrows the closest one we did (V-25) -- for
		; THIS call only. Set, say, clear on one stack: the audio is resolved at
		; Say time, so the borrowed voice never outlives the line and no save can
		; catch the actor wearing it.
		VoiceType borrowed = None
		If aiVoice != 0
			borrowed = Game.GetForm(aiVoice) as VoiceType
		EndIf
		; AAF's own sayTopic order, exactly: note what they wear, borrow, say, CLEAR,
		; and only if clearing did not bring back what they wore (another mod had an
		; override on them), put that back. Comparing while our borrowed voice was
		; still on either left it on for good or pinned a pointless override.
		VoiceType prior = speaker.GetVoiceType()
		If borrowed
			speaker.SetOverrideVoiceType(borrowed)
		EndIf
		; Four arguments: the decompiled base sources carry no defaults.
		speaker.Say(line, None, False, listener)
		If borrowed
			speaker.SetOverrideVoiceType(None)
			If prior && speaker.GetVoiceType() != prior
				speaker.SetOverrideVoiceType(prior)
			EndIf
		EndIf
		Rapport:Core.Trace("bark: " + Rapport:Core.FormIdText(aiFormID) + " says topic " + asSetID)
		Return
	EndIf

	If _api == None
		Return
	EndIf

	Actor target = Game.GetForm(aiFormID) as Actor
	If target == None
		Rapport:Core.DeferOrder(aiFormID)
		Rapport:Core.Trace("order: " + Rapport:Core.FormIdText(aiFormID) + " no longer resolves - " + asSetID + " was not carried out")
		Return
	EndIf

	; AN ACTOR WHOSE 3D IS GONE IS NOT SOMEONE TO CALL AAF ABOUT.
	;
	; Resolving is not the same as being here: fast travel leaves the form
	; perfectly resolvable and the actor unloaded. A scene was running in Diamond
	; City, the player fast travelled to Goodneighbor mid-drain, and the bridge
	; STOPPED POLLING -- no expression, no overlay, no scene ending, nothing timed
	; at all, until a save was reloaded. A Papyrus stack does not come back from
	; an AAF call, and asking AAF about somebody who is not there is the way to
	; find that out.
	;
	; Deferring is right for an aftermath mark -- Tick re-asks once the owner is
	; nearby. For an expression or a heat overlay there is no mark to defer, and
	; DeferOrder is a harmless no-op for an actor it does not know: those are
	; recovered instead by _wearing, which is written into the save, so the next
	; load clears anything stranded here. Moisturizer's own handler below has
	; guarded itself this way since it was written; this is the same check, moved
	; to where it covers every kind.
	If !target.Is3DLoaded()
		Rapport:Core.DeferOrder(aiFormID)
		Rapport:Core.RequeueOrder(aiKind, aiFormID, asSetID, asExtra)
		Rapport:Core.Trace("order: " + Rapport:Core.FormIdText(aiFormID) + " is not loaded - " + asSetID + " deferred rather than asked of AAF")
		Return
	EndIf

	If aiKind == 1
		_api.ApplyOverlaySet(target, asSetID)
		Rapport:Core.Trace("aftermath: asked AAF for " + asSetID + " on " + Rapport:Core.FormIdText(aiFormID))
	ElseIf aiKind == 2
		_api.RemoveOverlaySet(target, asSetID)
		Rapport:Core.Trace("aftermath: asked AAF to remove " + asSetID + " from " + Rapport:Core.FormIdText(aiFormID))
	ElseIf aiKind == 3
		_api.ApplyMFGSet(target, asSetID)
		; And hold it there, if asked to. AddMFGBlock is the half of this API
		; Rapport has never called -- we only ever REMOVED blocks, on clear. What
		; contends with us is the engine's own facial idle (blink, breathe, talk)
		; writing the same morphs, not the animation pack: the whole install has
		; ten mfgSet references and the pack playing here has one.
		;
		; Two AAF calls on one stack, which kind 5 below has always done. The
		; block comes off in the same place its lock does.
		If Rapport:Core.BlockFaces()
			_api.AddMFGBlock(target, Self.AllMorphIDs())
		EndIf
		Rapport:Core.Trace("face: asked AAF for " + asSetID + " on " + Rapport:Core.FormIdText(aiFormID))
	ElseIf aiKind == 4
		Self.ReleaseActor(target)
		Rapport:Core.Trace("released the AAF busy keywords from " + Rapport:Core.FormIdText(aiFormID))
	ElseIf aiKind == 15
		Self.QueryAnimationsFor(aiFormID, asSetID, asExtra)
		Return
	ElseIf aiKind == 30
		Self.ChangePositionRaw(aiFormID, asSetID, asExtra)
		Return
	ElseIf aiKind == 31
		Self.StartScenePos(aiFormID, asSetID, asExtra)
		Return
	ElseIf aiKind == 32
		If _api != None
			Actor who = Game.GetForm(aiFormID) as Actor
			If who == None
				Rapport:Core.Trace("actordata: " + Rapport:Core.FormIdText(aiFormID) + " is not a form we can resolve")
			Else
				Rapport:Core.Trace("actordata: asking AAF about " + Rapport:Core.FormIdText(aiFormID) + " - the answer, if any, arrives as OnActorData")
				_api.GetActorData(who)
			EndIf
		EndIf
		Return
	ElseIf aiKind == 28
		Self.QueryTagsRaw(aiFormID, asSetID, asExtra, "")
		Return
	ElseIf aiKind == 29
		Self.QueryTagsRaw(aiFormID, asSetID, asExtra, "NONE")
		Return
	ElseIf aiKind == 5
		; Both halves. The zeroed set puts every morph back to nothing; the block
		; removal is what lets go of them, because every expression Rapport
		; applies is locked and a morph left locked at zero is a face that can no
		; longer talk. No installed pack ships lock="false", so nothing here
		; demonstrates that applying zeros alone releases it -- and a frozen face
		; is exactly the failure this is meant to prevent.
		_api.ApplyMFGSet(target, asSetID)
		_api.RemoveMFGBlock(target, Self.AllMorphIDs())
		Rapport:Core.Trace("face: cleared " + asSetID + " from " + Rapport:Core.FormIdText(aiFormID))
	EndIf
EndFunction

; Clear the updater's saved SWF path, which is the whole bug.
;
; AAF_MainQuestScript.EveryTime_Initialization branches on
; AAF_UpdaterQuestScript.getSWFPath(): empty means "load the interface", anything
; else means "reboot the one already at this path". That variable lives in the
; SAVE, and the only thing that blanks it is the UPDATER's own
; EveryTime_Initialization -- which runs off its own OnPlayerLoadGame, not off
; the main quest's.
;
; Both quests extend AAF_QuestBase and both register for OnPlayerLoadGame, and
; Papyrus does not order delivery between them. That is the race:
;
;   updater first -> SWFPath blanked -> main sees "" -> UI.Load -> ready
;   main first    -> main reads the STALE path -> ui.Invoke(path + ".reboot")
;                    on a menu instance minted in a previous session, which no
;                    longer exists. No error. AAF is deaf for the session.
;
; Measured: the path was byte-identical across two restart attempts
; ("root1.instance166.instance165") where a real UI.Load mints a new instance
; number every time, and AAF recovered only when an ordinary save load happened
; to win the race the other way.
;
; So this makes the lucky ordering deterministic. It costs nothing when AAF is
; healthy: blanking that path is what AAF itself writes there on every load.
Function ClearStaleSWFPath()
	AAF:AAF_UpdaterQuestScript updater = Game.GetFormFromFile(132529, "AAF.esm") as AAF:AAF_UpdaterQuestScript
	If updater == None
		updater = Game.GetFormFromFile(132529, "AAF.esp") as AAF:AAF_UpdaterQuestScript
	EndIf

	If updater == None
		Rapport:Core.Trace("aaf watchdog: AAF's updater quest does not resolve - the interface can only be rebooted in place, not reloaded")
		Return
	EndIf

	Rapport:Core.Trace("aaf watchdog: updater swf path was \"" + updater.getSWFPath() + "\" - clearing it so AAF loads its interface instead of rebooting a menu that is gone")
	updater.EveryTime_Initialization()
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

	Self.ClearStaleSWFPath()

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

	; Before the restart, not only before the gentle cure. Start() runs AAF's
	; OneTime_Initialization, which runs EveryTime_Initialization, which reads the
	; same stale path -- so without this the restart re-enters the broken branch
	; and a whole quest stop buys nothing. That is exactly what happened on the
	; first attempt at it: the quest came back running and AAF stayed deaf.
	Self.ClearStaleSWFPath()

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

; The in-flight request one actor belongs to, by form id, or -1.
Int Function FindRequestByActor(Int aiFormID)
	If _inFlight == None
		Return -1
	EndIf
	Int i = 0
	While i < _inFlight.Length
		If _inFlight[i].first != None && _inFlight[i].first.GetFormID() == aiFormID
			Return i
		EndIf
		If _inFlight[i].second != None && _inFlight[i].second.GetFormID() == aiFormID
			Return i
		EndIf
		i += 1
	EndWhile
	Return -1
EndFunction

; Ask AAF what it would match for these two, with the tag a stage is trying.
;
; ChangePosition has been refused 26 times out of 26 for tags whose content
; exists -- five selectable female+male kissing positions, 273 female+male
; positions in all. Counting AAF's own XML says one thing and AAF says another,
; and it is AAF's answer that decides. This asks it directly.
;
; The reply arrives on OnAnimationQueryResult, which is already registered and
; already logs its arguments raw -- AAF passes that event straight through from
; its DLL, so the layout is not readable anywhere in its source and has to be
; learned from the game.
Function QueryAnimationsFor(Int aiFirstID, String asIncludeTags, String asExcludeTags)
	If _api == None
		Return
	EndIf

	Int index = Self.FindRequestByActor(aiFirstID)
	If index < 0
		Return
	EndIf

	Actor akFirst = _inFlight[index].first
	Actor akSecond = _inFlight[index].second
	If akFirst == None || akSecond == None
		Return
	EndIf

	Actor[] actors = Self.ForAAF(akFirst, akSecond)

	; Five arguments, all of them. The decompiled base sources carry no defaults.
	String label = asIncludeTags
	If label == ""
		label = "NOFILTER"
	EndIf

	_api.FindMatchingAnimations(actors, "Rapport:" + label, asIncludeTags, asExcludeTags, "")
	Rapport:Core.Trace("query: asked AAF what it matches for [" + label + "] on this pair - the answer comes back as OnAnimationQueryResult")
EndFunction

; A raw StartScene on a NAMED position. TESTING ONLY, and deliberately outside
; Rapport's own scene bookkeeping -- the mod will not recognise this scene.
;
; This is the control ChangePositionRaw needs. Every scene Rapport starts runs a
; positionTree, and a tree owns its navigation, so "ChangePosition does not work"
; and "a tree-driven scene cannot be moved" fit all 26 refusals equally well.
; Start a scene on a position that is not a tree, move THAT, and they separate.
;
; ONE AAF call on this stack; the next poll is already scheduled.
Function StartScenePos(Int aiFirstID, String asPosition, String asSecondID)
	If _api == None
		Rapport:Core.Trace("startpos: no AAF interface")
		Return
	EndIf

	Actor akFirst = Game.GetForm(aiFirstID) as Actor
	Actor akSecond = Game.GetForm(asSecondID as Int) as Actor
	If akFirst == None || akSecond == None
		Rapport:Core.Trace("startpos: one of those two is not a form we can resolve")
		Return
	EndIf

	AAF:AAF_API:SceneSettings settings = _api.GetSceneSettings()
	settings.position = asPosition
	settings.meta = "Rapport,control"

	Actor[] actors = Self.ForAAF(akFirst, akSecond)
	Rapport:Core.Trace("startpos: starting [" + asPosition + "] for " + Rapport:Core.FormIdText(aiFirstID) + " + " + asSecondID)
	_api.StartScene(actors, settings)
EndFunction

; A raw ChangePosition, three ways. TESTING ONLY -- nothing in the mod moves a
; running scene, and it is not coming back until this says something.
;
; The three arms exist because AAF's documentation disagrees with AAF's own code.
; GetPositionSettings() fills position, includeTags and combinedTags with None --
; "" for a Papyrus String -- and that is the factory his docs tell callers to use.
; The published signature of FindMatchingAnimations defaults those same fields to
; the literal string "NONE", and his review told us "" is what was wrong with our
; calls. Both cannot be right, so send all three and read the refusals.
;
; ONE AAF call on this stack, and the next poll is already scheduled.
Function ChangePositionRaw(Int aiFormID, String asPosition, String asArm)
	If _api == None
		Rapport:Core.Trace("changepos: no AAF interface")
		Return
	EndIf

	Actor target = Game.GetForm(aiFormID) as Actor
	If target == None
		Rapport:Core.Trace("changepos: " + Rapport:Core.FormIdText(aiFormID) + " is not a form we can resolve")
		Return
	EndIf

	; His factory, untouched: duration from the ini, excludeTags
	; "default_excludetags", and None in the other three.
	AAF:AAF_API:PositionSettings settings = _api.GetPositionSettings()

	If asArm == "none"
		settings.includeTags = "NONE"
		settings.combinedTags = "NONE"
	ElseIf asArm == "empty"
		settings.includeTags = ""
		settings.combinedTags = ""
	EndIf

	If asPosition != "-"
		settings.position = asPosition
	EndIf

	Rapport:Core.Trace("changepos: arm [" + asArm + "] position [" + asPosition + "] include [" + settings.includeTags + "] exclude [" + settings.excludeTags + "] combined [" + settings.combinedTags + "] duration " + settings.duration)
	_api.ChangePosition(target, settings)
	Rapport:Core.Trace("changepos: sent - whatever AAF says arrives as an event or an on-screen error, not as a return value")
EndFunction

; The same question QueryAnimationsFor asks, on demand, for any two actors, with
; the combinedTags value supplied by the caller rather than hardcoded to "".
;
; Both arms go to AAF verbatim -- that is the entire point. "" is what Rapport
; has always sent and "NONE" is what AAF's documentation gives as the default,
; and only asking the identical question both ways tells a content gap apart from
; a filter we have been getting wrong since the first version.
Function QueryTagsRaw(Int aiFirstID, String asIncludeTags, String asSecondID, String asCombinedTags)
	If _api == None
		Rapport:Core.Trace("query: no AAF interface")
		Return
	EndIf

	Actor akFirst = Game.GetForm(aiFirstID) as Actor
	Actor akSecond = Game.GetForm(asSecondID as Int) as Actor
	If akFirst == None || akSecond == None
		Rapport:Core.Trace("query: one of those two is not a form we can resolve")
		Return
	EndIf

	Actor[] actors = Self.ForAAF(akFirst, akSecond)
	String arm = "NONE"
	If asCombinedTags == ""
		arm = "EMPTY"
	EndIf
	String label = "RAW-" + arm + "-" + asIncludeTags

	_api.FindMatchingAnimations(actors, "Rapport:" + label, asIncludeTags, "default_excludetags", asCombinedTags)
	Rapport:Core.Trace("query: asked AAF include [" + asIncludeTags + "] exclude [default_excludetags] combined [" + asCombinedTags + "] - the answer arrives as OnAnimationQueryResult labelled " + label)
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
			Rapport:Core.Trace("takeover: " + name + " (" + Rapport:Core.FormIdText(formID) + ") did not resolve - left alone")
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
