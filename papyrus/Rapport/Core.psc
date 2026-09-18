Scriptname Rapport:Core Native Hidden
{Everything Papyrus can ask of Rapport.dll. Native functions have to live in a
 script flagged Native, which a Quest script cannot be, so they sit here and the
 bridge calls them by name.

 Note the direction: Papyrus calls in, C++ never calls out. Dispatching into the
 VM from the plugin crashed the game twice inside DispatchMethodCallImpl, because
 F4SE tasks run on a BSJobs job thread while the VM's argument packing uses the
 per-thread scrap heap. Asking rather than being told keeps every VM operation on
 the VM's own thread.}

; ---- the doorbell --------------------------------------------------------
; Claims the next scene the scheduler wants started and returns its request id,
; or 0 when there is nothing to do. Cheap by design: almost every call returns 0.
Int Function TakeRequest() Global Native

; Details of the request TakeRequest just handed out. Form ids, not objects.
Int Function TakenFirstID() Global Native
Int Function TakenSecondID() Global Native
Float Function TakenDuration() Global Native

; True until the bridge has introduced itself in THIS session. The script cannot
; work this out for itself: its own variables persist in the save while the
; plugin starts from nothing, so a script that remembers connecting will never
; re-register its event handlers after a reload.
Bool Function NeedsHandshake() Global Native

; How often the bridge should ask, in seconds. Read from the ini, so the poll
; interval is not a number buried in a script.
Float Function PollSeconds() Global Native

; Every AAF event the bridge receives, by name. The plugin counts them so that
; "no AAF event has ever arrived" is something the log SAYS rather than something
; a reader has to notice is missing.
Function NoteEvent(String asEventName) Global Native

; ---- the debug hub -------------------------------------------------------
; Settings Rapport wants applied to OTHER mods, read from debug.json. The plugin
; cannot apply these itself: AAF and MCM are both Papyrus-side.
Int Function DebugCount() Global Native
String Function DebugTarget(Int aiIndex) Global Native
String Function DebugMod(Int aiIndex) Global Native
String Function DebugKey(Int aiIndex) Global Native
String Function DebugType(Int aiIndex) Global Native
String Function DebugValue(Int aiIndex) Global Native

; An actor AAF refused as busy. The scheduler sets them aside for a while rather
; than offering the same unusable pair on every tick.
Function NoteActorBusy(Int aiFormID) Global Native

; ---- aftermath -----------------------------------------------------------
; The tags AAF put on an animation, as one string. Only the bridge hears them,
; and the plugin is what decides what a scene leaves behind, so they come over
; raw and are parsed on the other side.
Function NoteSceneTags(String asTags) Global Native

; The second doorbell. 0 when there is nothing, 1 to apply the set, 2 to remove
; it. Collected on the same poll as the scene doorbell, for the same reason: the
; plugin cannot call AAF, and Papyrus is the only side that can.
Int Function TakeOverlayOrder() Global Native
Int Function OrderActorID() Global Native
String Function OrderSetID() Global Native

; ---- takeover ------------------------------------------------------------
; Quests belonging to OTHER mods that Rapport stops while it owns a feature they
; also do. The plugin resolves them against the load order; stopping a quest is
; game state, so it happens here.
Int Function TakeoverCount() Global Native
Int Function TakeoverFormID(Int aiIndex) Global Native
String Function TakeoverName(Int aiIndex) Global Native
String Function TakeoverReason(Int aiIndex) Global Native

; False when Rapport no longer owns the feature -- then the same list is STARTED
; again instead, which is what makes the takeover reversible rather than a
; permanent edit to somebody else's mod.
Bool Function TakeoverShouldStop() Global Native

; ---- reporting back ------------------------------------------------------
Function BridgeReady(Bool abAafPresent) Global Native
Function SceneStarted(Int aiRequest) Global Native
Function SceneEnded(Int aiRequest) Global Native
Function RequestFailed(Int aiRequest, String asWhy) Global Native

; One log for the whole mod. Papyrus writes into Rapport.log rather than into a
; second file that nobody thinks to read next to the first.
Function Trace(String asText) Global Native
