Scriptname Rapport:Bridge extends Quest
{The only Papyrus in Rapport. AAF's API is Papyrus-only, so something has to live
 here; by the time a call arrives, everything that could be decided natively has
 been. This script starts scenes, watches AAF's events, and reports back.

 Nothing here polls, and no script is ever attached to an actor.

 Style note: the base sources are decompiled and so carry no default argument
 values. Every argument is passed explicitly. See docs/papyrus-toolchain.md.}

Int Property kPollTimer = 1 AutoReadOnly
Int Property kSceneTimer = 2 AutoReadOnly

Struct Request
  Int id
  Actor first
  Actor second
EndStruct

AAF:AAF_API _api
Request[] _inFlight
Bool _ready = false

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

	; Both of these must happen on every load, not only the first: the timer may
	; not have survived, and the plugin has no memory of the last session.
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
	If aiTimerID == kSceneTimer
		If _inFlight.Length > 0
			Rapport:Core.Trace("bridge: giving up on request " + _inFlight[0].id + " - AAF never reported a scene")
			Self.Release(0, "AAF never started the scene")
		EndIf
		Return
	EndIf

	If aiTimerID != kPollTimer
		Return
	EndIf

	; Registrations do not survive a recompile, and _ready does survive the save,
	; so the script alone can end up permanently deaf: connected in its own memory,
	; registered for nothing. The plugin's fresh-every-session state is the only
	; reliable trigger for re-establishing them.
	If Rapport:Core.NeedsHandshake()
		Self.Connect()
	EndIf

	If _ready
		Int request = Rapport:Core.TakeRequest()
		If request != 0
			Self.BeginRequest(request, Rapport:Core.TakenFirstID(), Rapport:Core.TakenSecondID(), Rapport:Core.TakenDuration())
		EndIf
	EndIf

	Self.StartTimer(Rapport:Core.PollSeconds(), kPollTimer)
EndEvent

Function BeginRequest(Int aiRequest, Int aiFirstID, Int aiSecondID, Float afDuration)
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
		Rapport:Core.RequestFailed(aiRequest, "the first actor is already busy in AAF")
		Return
	EndIf
	If Self.IsOccupied(akSecond)
		Rapport:Core.RequestFailed(aiRequest, "the second actor is already busy in AAF")
		Return
	EndIf

	Request entry = new Request
	entry.id = aiRequest
	entry.first = akFirst
	entry.second = akSecond
	_inFlight.Add(entry, 1)

	Actor[] actors = new Actor[2]
	actors[0] = akFirst
	actors[1] = akSecond

	AAF:AAF_API:SceneSettings settings = _api.GetSceneSettings()
	settings.duration = afDuration
	settings.usePackages = true          ; AAF walks them there; we do not fight its packages
	settings.skipWalk = false
	settings.isNPCControlled = true
	settings.preventFurniture = false
	settings.meta = "Rapport,autonomy"   ; so a scene of ours is identifiable as ours

	Rapport:Core.Trace("bridge: request " + aiRequest + " starting for " + akFirst.GetFormID() + " and " + akSecond.GetFormID() + ", aaf status " + _api.GetAAFStatus())
	_api.StartScene(actors, settings)

	; A scene that never begins must not wedge the framework. AAF answers with an
	; event or it does not answer at all, and the first run of this code sat on
	; "a scene is already running" for three minutes because nothing ever came back.
	Self.StartTimer(afDuration + 60.0, kSceneTimer)
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
	Int index = Self.FindRequestByActors(akArgs)
	If index >= 0
		Rapport:Core.SceneStarted(_inFlight[index].id)
	EndIf
EndEvent

Event AAF:AAF_API.OnAnimationStart(AAF:AAF_API akSender, Var[] akArgs)
	Self.TraceArgs("OnAnimationStart", akArgs)
EndEvent

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
	; AAF identifies a scene by its actors, and the argument layout is not yet
	; known from anything but a trace. While MaxConcurrentScenes is 1 there is at
	; most one request to match, so position is enough and is honest about it.
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
