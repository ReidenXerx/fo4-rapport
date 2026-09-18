Scriptname Rapport:Bridge extends Quest
{The only Papyrus in Rapport. AAF's API is Papyrus-only, so something has to live
 here; by the time a call arrives, everything that could be decided natively has
 been. This script starts scenes, watches AAF's events, and reports back.

 Nothing here polls, and no script is ever attached to an actor.

 Style note: the base sources are decompiled and so carry no default argument
 values. Every argument is passed explicitly. See docs/papyrus-toolchain.md.}

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

Function Connect()
	If _ready
		Return
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

	; AAF_API re-broadcasts every event MainQuestScript sends, so these register
	; on the API object rather than on the quest behind it.
	RegisterForCustomEvent(_api, "OnAAFReady")
	RegisterForCustomEvent(_api, "OnSceneInit")
	RegisterForCustomEvent(_api, "OnSceneEnd")
	RegisterForCustomEvent(_api, "OnAnimationStart")
	RegisterForCustomEvent(_api, "OnAnimationStop")
	RegisterForCustomEvent(_api, "OnAnimationQueryResult")

	_ready = true
	Rapport:Core.Trace("bridge: connected to AAF " + _api.GetVersion() + " build " + _api.GetBuild())
	Rapport:Core.BridgeReady(true)
EndFunction

;---------------------------------------------------------------------------
; Called by the native scheduler once it has chosen a pair.
;---------------------------------------------------------------------------

Function BeginRequest(Int aiRequest, Actor akFirst, Actor akSecond, Float afDuration)
	If !_ready || _api == None
		Rapport:Core.RequestFailed(aiRequest, "bridge not connected")
		Return
	EndIf

	If akFirst == None || akSecond == None || akFirst == akSecond
		Rapport:Core.RequestFailed(aiRequest, "invalid pair")
		Return
	EndIf

	; Reserve both actors before anything moves them. AAF's own lock is what stops
	; a second system picking the same NPC, and taking it late is exactly the
	; window in which that happens.
	If !_api.SetActorLocked(akFirst, true)
		Rapport:Core.RequestFailed(aiRequest, "could not reserve the first actor")
		Return
	EndIf
	If !_api.SetActorLocked(akSecond, true)
		_api.SetActorLocked(akFirst, false)
		Rapport:Core.RequestFailed(aiRequest, "could not reserve the second actor")
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

	Rapport:Core.Trace("bridge: request " + aiRequest + " starting for " + akFirst.GetFormID() + " and " + akSecond.GetFormID())
	_api.StartScene(actors, settings)
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
	Rapport:Core.Trace("aaf: ready")
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

	If _api != None
		If entry.first != None
			_api.SetActorLocked(entry.first, false)
		EndIf
		If entry.second != None
			_api.SetActorLocked(entry.second, false)
		EndIf
	EndIf

	_inFlight.Remove(aiIndex, 1)

	If asWhy == ""
		Rapport:Core.SceneEnded(entry.id)
	Else
		Rapport:Core.RequestFailed(entry.id, asWhy)
	EndIf
EndFunction

Function TraceArgs(String asEvent, Var[] akArgs)
	If akArgs == None
		Rapport:Core.Trace("aaf: " + asEvent + " (no args)")
		Return
	EndIf

	String line = "aaf: " + asEvent + " args[" + akArgs.Length + "]"
	Int i = 0
	While i < akArgs.Length && i < 8
		line = line + " [" + i + "]=" + akArgs[i]
		i += 1
	EndWhile
	Rapport:Core.Trace(line)
EndFunction
