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

; ---- reporting back ------------------------------------------------------
Function BridgeReady(Bool abAafPresent) Global Native
Function SceneStarted(Int aiRequest) Global Native
Function SceneEnded(Int aiRequest) Global Native
Function RequestFailed(Int aiRequest, String asWhy) Global Native

; One log for the whole mod. Papyrus writes into Rapport.log rather than into a
; second file that nobody thinks to read next to the first.
Function Trace(String asText) Global Native
