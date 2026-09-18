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

; The request whose scene has run for as long as we asked, or 0. AAF does not
; enforce the duration it is given, so somebody has to -- and it is the plugin,
; because the bridge has exactly ONE timer that is known to work. Every attempt
; to start a second one killed the function that started it.
Int Function SceneToStop() Global Native

; Said once, after StopScene has been asked for, so the poll does not ask again
; every three seconds while AAF winds the scene down.
Function NoteStopAsked() Global Native

; ---- aftermath -----------------------------------------------------------
; The tags AAF put on an animation, as one string. Only the bridge hears them,
; and the plugin is what decides what a scene leaves behind, so they come over
; raw and are parsed on the other side.
Function NoteSceneTags(String asTags) Global Native

; An actor's sex: 0 male, 1 female, anything else unknown. Reported from Papyrus
; because only Papyrus holds a real Actor to ask.
Function NoteActorSex(Int aiFormID, Int aiSex) Global Native

; The order AAF actually placed the two of them in. Slot 0 is the receiving role
; in 559 of the 562 two-actor animations that name both genders, and it is the
; only thing that tells a same-sex pair apart.
Function NoteSceneSlots(Int aiSlot0, Int aiSlot1) Global Native

; Called at the top of every poll, before anything is collected. Everything that
; has to happen on a clock finer than the scheduler's twenty seconds lives behind
; it: the expression progression through a scene, and the clearing afterwards.
Function Pump() Global Native

; The second doorbell. 0 when there is nothing, then:
;   1  apply an overlay set      2  remove an overlay set
;   3  apply a facial expression 4  take AAF's busy keywords off this actor
;   5  clear a facial expression -- the zeroed set AND the block removal
; Collected on the same poll as the scene doorbell, for the same reason: the
; plugin cannot call AAF, and Papyrus is the only side that can.
Int Function TakeOverlayOrder() Global Native
Int Function OrderActorID() Global Native
String Function OrderSetID() Global Native

; An order the bridge collected but could not carry out -- an actor who turned
; out not to be loaded. Puts the mark back to "not asked for yet" so the next
; tick offers it again, because a collected order is gone whether or not anything
; actually happened.
Function DeferOrder(Int aiFormID) Global Native

; ---- the optional Commonwealth Moisturizer plugin -------------------------
; Its own doorbell, drained by Rapport:Moisturizer in Rapport_Moisturizer.esp.
; It is a separate script in a separate plugin for one reason: naming a
; Moisturizer type inside Rapport:Bridge would leave an unresolvable reference in
; the bridge on every install that does not have that mod.
;
; 0 nothing, 6 apply, 7 clear. The regions are some subset of "FOR" -- front,
; oral, rear -- which is exactly the shape its own API takes.
Int Function TakeMoisturizerOrder() Global Native
Int Function MoisturizerActorID() Global Native

; The regions as three answers rather than one string. Papyrus cannot search a
; string without F4SE's StringUtil, and the plugin already knows which are set.
Bool Function MoisturizerFront() Global Native
Bool Function MoisturizerOral() Global Native
Bool Function MoisturizerRear() Global Native

; The same thing as text, for the log only.
String Function MoisturizerRegions() Global Native

; How many times to ask for cum in the same place. Its picker skips slots that
; are already used rather than re-rolling them, so this stacks distinct decals.
Int Function MoisturizerLayers() Global Native

; True when Rapport has chosen Moisturizer as its aftermath backend. False means
; stand down entirely -- either CumOverlays is driving or the feature is off.
Bool Function MoisturizerWanted() Global Native

; ---- takeover ------------------------------------------------------------
; Quests belonging to OTHER mods that Rapport stops while it owns a feature they
; also do. The plugin resolves them against the load order; stopping a quest is
; game state, so it happens here.
Int Function TakeoverCount() Global Native
Int Function TakeoverFormID(Int aiIndex) Global Native
String Function TakeoverName(Int aiIndex) Global Native
String Function TakeoverReason(Int aiIndex) Global Native

; False when Rapport no longer owns the feature THIS entry belongs to -- then it
; is STARTED again instead, which is what makes the takeover reversible rather
; than a permanent edit to somebody else's mod.
Bool Function TakeoverShouldStop(Int aiIndex) Global Native

; ---- reporting back ------------------------------------------------------
Function BridgeReady(Bool abAafPresent) Global Native
Function SceneStarted(Int aiRequest) Global Native
Function SceneEnded(Int aiRequest) Global Native
Function RequestFailed(Int aiRequest, String asWhy) Global Native

; One log for the whole mod. Papyrus writes into Rapport.log rather than into a
; second file that nobody thinks to read next to the first.
Function Trace(String asText) Global Native
