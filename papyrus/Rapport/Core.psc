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
; Onlookers free before the crowd penalty starts (scoring observerTolerance, or MCM).
Int Function ObserverTolerance() Global Native

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

; Whether AAF has ANY scene running right now -- anybody's, not only ours.
; Reported from OnSceneInit/OnSceneEnd, which are broadcast for every scene on
; the system. A busy flag is only called stale when nothing at all is running.
Function NoteSceneLive(Int aiSceneID) Global Native
Function NoteSceneEnded(Int aiSceneID) Global Native

; Starts the clock on how long we have been listening. Nothing is called stale
; until a scene that began before we connected could no longer be running.
Function NoteBridgeConnected() Global Native

; Should an applied expression also BLOCK everything else from moving the face?
; Rapport.ini, BlockAnimationFaces. The engine's facial idle writes the same
; morphs to blink, breathe and talk, and two writers on one morph is a flicker.
Bool Function BlockFaces() Global Native

; The request whose scene has run for as long as we asked, or 0. AAF does not
; enforce the duration it is given, so somebody has to -- and it is the plugin,
; because the bridge has exactly ONE timer that is known to work. Every attempt
; to start a second one killed the function that started it.
Int Function SceneToStop() Global Native

; ---- the AAF watchdog ----------------------------------------------------
; What AAF says about itself, reported on EVERY poll and immediately before
; Pump, so the watchdog can never reason about a stale number.
;
; GetAAFStatus() is AAF_ReadyStatus + 1, except that it returns 0 when AAF's
; main quest is not running at all. Those are two different failures needing two
; different cures, and this framework spent a session collapsing them:
;
;   -1  ours: the bridge could not ask (no API)
;    0  AAF's main quest is stopped
;    1  running, but its interface has never announced itself
;    2  ready
;
; abHUDReady is UI.IsMenuOpen("HUDMenu") -- the exact condition the cure needs,
; not a proxy for it. Every restart AAF performs ends in
; UI.Load("HUDMenu", "root1", "AAF.swf", ...), and F4SE's own UI.psc spells out
; that shape: the asset loads as a child of a variable inside a menu that has to
; be there already. On a load screen it is not, which is how AAF lost its reboot
; to begin with -- and an attempt spent that way is counted without anything
; having been tried.
; AAF_Version as AAF reports it (1741 = 1.7.4.1).
Function NoteAAFVersion(Int aiVersion) Global Native
Function NoteAAFStatus(Int aiStatus, Bool abHUDReady) Global Native

; The player's answer to "AAF's quest is not running - start it?". Recorded so
; that a no is a no for the session, and so the log says which of the two it was.
Function NoteAAFRevivalChoice(Bool abYes) Global Native

; The position a scene should START on, chosen from the tree catalogue for the
; scenario this request named. Empty means start unconstrained.
;
; Asked immediately before StartScene, and only there. AAF's ChangePosition does
; not work -- refused 26 times out of 26 with tags, and refused again when handed
; a position id and no filters at all -- so the one moment a position can be
; chosen is the moment the scene begins. StartScene honours it.
String Function ScenePosition() Global Native

; Said once, after StopScene has been asked for, so the poll does not ask again
; every three seconds while AAF winds the scene down.
Function NoteStopAsked() Global Native

; ---- aftermath -----------------------------------------------------------
; The tags AAF put on an animation, as one string. Only the bridge hears them,
; and the plugin is what decides what a scene leaves behind, so they come over
; raw and are parsed on the other side.
; AAF refused what a stage asked for. It reports this down OnSceneInit with four
; arguments instead of eleven; the scenario tries its next alternative.
Function SceneRefused(String asWhy) Global Native

; AAF refused the scene we asked for, and the refusal carried OUR meta tag. Fails
; the request in flight instead of leaving it to the 780-second watchdog, which
; is thirteen minutes of both actors flagged busy in AAF and the framework
; reporting "a scene is already running" to everything that asks.
; Returns False when there was nothing in flight for the refusal to be about.
Bool Function RefusedOurScene(String asWhy) Global Native

Function NoteSceneTags(String asTags) Global Native

; The position AAF is playing, by NAME. Rapport uses it two ways: as the key for a
; hand-written override, and -- when the tags name no act at all -- as words to read.
; Measured: five positions on the reference install say "Blowjob" in the title and
; nothing in their tags.
Function NoteScenePosition(String asPosition) Global Native

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
;   6  ask the player whether to start AAF's quest
;   7  release an actor AAF left flagged busy
; There is no kind 8. It moved a running scene to a scenario's next stage by
; calling ChangePosition, which AAF refused 26 times out of 26 -- with tags,
; with a position id, and with no filters at all. Staging is AAF's now: Rapport
; picks the tree the scene STARTS on and AAF walks it.
; Collected on the same poll as the scene doorbell, for the same reason: the
; plugin cannot call AAF, and Papyrus is the only side that can.
Int Function TakeOverlayOrder() Global Native

; How many orders are still waiting. The bridge logs this when its per-poll
; budget of eight runs out, so a backlog is something the log SAYS rather than
; something a reader has to infer from orders arriving late.
Int Function PendingOrders() Global Native
Int Function OrderActorID() Global Native
String Function OrderSetID() Global Native

; Only order kind 8 uses this: the tags AAF must AVOID for this stage. The
; include tags come through OrderSetID.
String Function OrderExtra() Global Native
Int Function OrderVoice() Global Native
Float Function WatchRadius() Global Native
Int Function WatchFirstID() Global Native
Int Function WatchSecondID() Global Native
Bool Function NoteWatcher(Int aiFormID, Bool abSees) Global Native
Function EndWatchSweep() Global Native

; An order the bridge collected but could not carry out -- an actor who turned
; out not to be loaded. Puts the mark back to "not asked for yet" so the next
; tick offers it again, because a collected order is gone whether or not anything
; actually happened.
Function DeferOrder(Int aiFormID) Global Native

; The order the bridge was just handed, which it could not carry out because the
; actor is not loaded. Takes no arguments: the plugin still has that order
; latched, so it re-forms it rather than trusting a second description of it.
;
; Only CLEANUP is held -- a removal stays correct however long it waits, an
; application does not. Everything else is dropped, and _wearing clears it on the
; next load.
Function RequeueOrder(Int aiKind, Int aiFormID, String asSetID, String asExtra) Global Native

; A form id as "FF004C9B (-16757605)" -- hex first because that is what AAF's log,
; xEdit and the console all say, decimal after because that is what every earlier
; line of this log says. Papyrus has no hex formatter and its Int is signed, so
; every FF-prefixed (dynamically created) form prints negative on its own, and
; those are most of what autonomy pairs.
String Function FormIdText(Int aiFormID) Global Native

; ---- what Rapport:Medic asks ----------------------------------------------
; Both exist so the medic never has to touch the bridge to find out how the
; bridge is doing. Asking the patient whether it is breathing is not a check.

; Consecutive plugin ticks in which the bridge has not polled once. A tick is
; PollSeconds-independent -- it is the plugin's own 20s scheduler pass -- so 2 is
; roughly forty seconds of silence. 0 means it polled this tick.
Int Function BridgeSilentTicks() Global Native

; Give up on the scene in flight: forget the request, end the scenario, take the
; faces off and hand both actors back to AAF. Returns False when there was
; nothing in flight, which is how the medic tells a real heal from a re-armed
; clock -- and therefore whether the player is owed an explanation.
Bool Function AbandonInFlight(String asWhy) Global Native

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

; ---- the addon door, part one: WHICH TWO ---------------------------------
;
; Rapport scores, you select.
;
; Every pass, Rapport enumerates the loaded actors, drops the children, the wrong
; races, the quest actors with running packages and the hostiles, ranks every
; remaining pair, and publishes what it measured. None of that is policy and you
; should not reimplement any of it. What it deliberately does NOT decide is
; whether any of those pairs is worth acting on -- that is yours.
;
; Index 0 is the best-scoring pair. Out of range gives 0 or False, which is how
; the loop finds the end:
;
;   Int i = 0
;   While i < Rapport:Core.CandidateCount()
;       If !Rapport:Core.CandidatePlayerNear(i) && Rapport:Core.CandidateObservers(i) < 2
;           Actor a = Game.GetForm(Rapport:Core.CandidateFirst(i)) as Actor
;           Actor b = Game.GetForm(Rapport:Core.CandidateSecond(i)) as Actor
;           If a != None && b != None && Rapport:Core.CanRun("athome", a, b) >= 2
;               Rapport:Core.RequestScene(a, b, "athome")
;               Return
;           EndIf
;       EndIf
;       i += 1
;   EndWhile
;
; FORM IDS, not Actors, and the reason matters: the pointers behind a scored pair
; are only valid on the tick that produced them. You read this later, on your own
; timer, and an actor can unload in between. A stale id resolves to None through
; Game.GetForm and you skip it; a stale pointer would be a crash in your mod.
Int Function CandidateCount() Global Native
Int Function CandidateFirst(Int aiIndex) Global Native
Int Function CandidateSecond(Int aiIndex) Global Native

; Rapport's own ranking of the pair. The bar it would have used is MinimumScore in
; Rapport.ini, but nothing enforces it -- the number is reported, never applied.
Float Function CandidateScore(Int aiIndex) Global Native

; The raw measurements behind that score, so you can weigh them differently.
; Distance is in game units. Observers counts uninvolved living actors who could
; see the spot, and does not include the player -- ask separately.
Float Function CandidateDistance(Int aiIndex) Global Native
Int Function CandidateObservers(Int aiIndex) Global Native
Bool Function CandidatePlayerNear(Int aiIndex) Global Native
Bool Function CandidateInterior(Int aiIndex) Global Native
Bool Function CandidateNight(Int aiIndex) Global Native
Bool Function CandidateSharedFaction(Int aiIndex) Global Native

; An actor's display name, because Papyrus cannot get one.
;
; Fallout 4's base Papyrus has no name accessor at all -- no GetName on Form, no
; GetDisplayName on ObjectReference, nothing on Actor. So an addon's log is a wall
; of form ids unless the framework hands the name over, and an unreadable diagnostic
; log is a diagnostic log that has failed.
;
; Empty string for None or for an actor with no name.
String Function ActorName(Actor akWho) Global Native

; ---- the addon door, part three: WHAT HAPPENED BEFORE --------------------
;
; Rapport keeps these in the SAVE, per actor, so a player with three characters
; has three sets of them. It records the facts and decides nothing with them:
; "too soon", "bored of this partner" and "wants company" are yours.
;
; All take a FORM ID, the same one CandidateFirst/Second gave you.

; Game hours since this actor's last scene. A very large number (1e9) when they
; have never had one, so a cooldown test needs no special case and "longest
; since" sorts correctly.
Float Function HoursSinceScene(Int aiFormID) Global Native

; Who it was with, and how many they have had. lastPartner is the MOST RECENT
; partner only -- it is not a history, so "have these two ever" is only knowable
; while neither has been with anyone else since.
Int Function LastPartner(Int aiFormID) Global Native
Int Function SceneCount(Int aiFormID) Global Native

; The same two facts about a PAIR, which the per-actor records cannot answer once
; either of them has been with somebody else. Order does not matter: (A,B) and
; (B,A) are one record.
;
; This is what a repeat-pairing bonus needs. LastPartner alone decays the moment
; anyone moves on, so couples could never emerge from it.
Float Function HoursSincePair(Int aiFirst, Int aiSecond) Global Native
Int Function PairSceneCount(Int aiFirst, Int aiSecond) Global Native
; The relationship store (R-1). Bond is -1 (enemies) .. +1 (closest); AddBond returns the new
; value (reason 3 dialogue, 4 gift, 5 other addon). The curve that turns it into behaviour is yours.
Float Function PairBond(Int aiFirst, Int aiSecond) Global Native
Float Function AddBond(Int aiFirst, Int aiSecond, Float afAmount, Int aiReason) Global Native
; BLOOD relatives (siblings, parent/child, grandparents, aunts/uncles, cousins). A FLAG for
; consumers that judge it (NPC attitudes: a 'bad thing'), NEVER a refusal - owner, R-14.
Bool Function IsIncestPair(Int aiFirst, Int aiSecond) Global Native
; Spouse or courting, per the engine.
Bool Function IsPartnerPair(Int aiFirst, Int aiSecond) Global Native
; Set when one of the two was partnered to someone ELSE at their scene (a future "bad thing").
Bool Function IsAffairPair(Int aiFirst, Int aiSecond) Global Native
; Staged: recorded only if the requested scene actually starts.
Function NoteAffair(Int aiFirst, Int aiSecond) Global Native
; Internal - use Rapport:Relations.BondBetween.
Float Function PreviewBond(Int aiFirst, Int aiSecond, Int aiRank, Bool abPartner) Global Native
; Bumped every time the candidate list is republished (every 20s). Read it before and after
; reading the list across several calls; if it moved, read again.
Int Function CandidateGeneration() Global Native
; major*10000 + minor*100 + patch - check it on connect.
Int Function ApiVersion() Global Native
; 0 (strays freely) .. 1 (never looks elsewhere): derived from the form id, the same forever.
Float Function FaithfulnessOf(Int aiFormID) Global Native
; The Narrator. NarrateBonus: your addon's share of a pair's score, reported just BEFORE
; RequestScene ("bond", 0.45). NarrateNearMiss: you passed on a likely pair - asWhy is a
; clause with no names ("too many people are watching"); Rapport rate-limits it.
Function NarrateBonus(Int aiFirst, Int aiSecond, String asLabel, Float afValue) Global Native
Function NarrateNearMiss(Int aiFirst, Int aiSecond, String asWhy, Float afScore, Float afBar) Global Native
String Function NarratorHistory() Global Native
; An addon's OWN moment, in its own words: one short line in the storyteller's
; voice (Overture: how a conversation went, and why). {first} and {second} in
; either text become the two names. asNumbers is the second line, shown only with
; the Narrator's numbers switch on. Gated by the "addon moments" switch, kept in the
; history. ApiVersion 201+.
Function NarrateLine(Int aiFirst, Int aiSecond, String asHeadline, String asNumbers) Global Native
; The player's priority lane (owner, 2026-09-23: a player's own request outranks
; autonomy). Holds Rapport's one scene slot for the PLAYER and akWith for up to
; afSeconds (at most 120; 0 lets go): every other pair's RequestScene is refused
; until this pair's own request is accepted or the time runs out. A scene already
; running is never cut short -- the hold takes the next free slot. Overture holds
; it from the moment a proposition would be a yes. ApiVersion 201+.
Function ReservePlayerScene(Actor akWith, Float afSeconds) Global Native
; Names for the nameless. The first time an addon introduces a generic NPC (a base
; not flagged Unique), Rapport gives them a first name and a surname -- derived from
; the form id, so the same person gets the same name forever -- and returns it.
; Empty if they have a real name, already carry a custom one, were introduced
; before, or names are off. The rename happens on the main thread, a frame later.
; ApiVersion 201+.
String Function Introduce(Actor akWho) Global Native
; True once the engine's relationship has been imported for this pair (its first interaction).
Bool Function IsPairSeeded(Int aiFirst, Int aiSecond) Global Native
; The bond a pair WOULD start at, written nowhere. Rapport:Relations.BondBetween wraps it.
Float Function SeedBond(Int aiRank, Bool abPartner) Global Native
Function NoteVanillaRelationship(Int aiFirst, Int aiSecond, Int aiRank, Bool abBlood, Bool abPartner) Global Native
; How many people could see this actor right now -- loaded, alive, people only,
; children never counted, measured against the same observerRadius Rapport's own
; pairing score uses. The actor themselves and the PLAYER are both excluded: the
; one being propositioned is not in public merely because the player is there.
; Returns -1 if the form id is not an actor we can see, which is NOT the same as
; nobody watching. ApiVersion 201+.
Int Function ObserversNear(Int aiFormID) Global Native

String Function PersonaOf(Int aiFormID) Global Native

; Requests involving this actor that AAF turned down, and how long ago the last one
; was. Use BOTH: a cumulative count with no recency would avoid an actor forever
; over two failures a week ago, and recency alone cannot tell a one-off from an NPC
; who is permanently stuck.
;
; Together they give an escalating backoff, which is what this actually needs. AAF
; silently refuses an actor carrying its busy keywords, and a request that died
; without cleaning up leaves that flag on an NPC for the rest of the save -- at
; which point they fail every time and cost a poll each.
;
; A very large number (1e9) when they have never been refused.
Int Function RefusalCount(Int aiFormID) Global Native
Float Function HoursSinceRefusal(Int aiFormID) Global Native

; YOUR number, per actor, kept in Rapport's co-save on your behalf so you do not
; have to build a second one for a single float. Rapport never reads it and has
; no opinion about what it means -- arousal, attraction, mood, a counter.
;
; USE IT SPARINGLY, and this is a save-health warning rather than style advice.
; SetNeed CREATES a record for that actor. A record with no scene and no refusal
; has no timestamp to age against, so it can only be pruned by being EMPTY --
; which means a non-zero need on an actor nothing else ever happens to persists
; for the rest of the playthrough.
;
; So: write it for actors that matter, not for every candidate you look at, and
; SetNeed(id, 0.0) when you are done with one. Zero is the value that lets the
; record be collected. There is a hard ceiling behind this as a backstop, but it
; drops the oldest records to hold the line, which means losing real history.
Float Function GetNeed(Int aiFormID) Global Native
Function SetNeed(Int aiFormID, Float afNeed) Global Native

; Say this ONCE, at startup, and Rapport stops starting scenes on its own.
;
; Rapport ships a stand-in decision so the framework can be tested without an
; addon, and it takes the best pair whenever it clears the bar. Two mods doing that
; means two mods reserving the same actors. This is a call rather than a setting
; because whether Rapport should decide depends on what is INSTALLED, and an ini
; that has to be edited to match is an ini that will be wrong.
;
; It stops the deciding AND the 24-hour cooldown filter, which is stand-in policy
; too -- that filter runs before ranking, so leaving it on would mean you never
; see a resting pair and could not apply a rule of your own. Scoring, publishing,
; the faces, the aftermath and the scene lifecycle all carry on.
;
; So once you call this, enforcing a cooldown is YOUR job. HoursSinceScene is the
; fact; what counts as too soon is the policy.
Function TakeOverDecisions(String asWho) Global Native

; ---- the addon door, part two: ASK FOR IT --------------------------------
; The only two functions another mod needs. Everything above is the bridge
; talking to the plugin; these are yours.
;
; Rapport is the framework and it does not decide WHEN anyone has sex -- an
; addon does, and then asks for a scenario BY NAME. A name is the only thing
; whose meaning survives a different install: the tags, the trees and the
; positions differ on every machine, and an addon naming those would be
; choosing from a catalogue it cannot see.
;
; Names ship in Data/F4SE/Plugins/Rapport/scenarios.json. Right now:
;   "quickie"  somewhere public, short, nothing removed, no guaranteed ending
;   "athome"   five stages, unhurried, ends in a climax
;   "tender"   three stages, slow, ends in a climax

; True while a scene of ours is running or starting. Check it FIRST and skip the
; whole pass: RequestScene declines while busy anyway, but by then you have read
; every candidate, scored them and printed a decision you cannot use. A scene lasts
; minutes and a poll lasts seconds.
Bool Function Busy() Global Native

; Ask FIRST, before you walk two actors anywhere. Bigger is better:
;   -1  no scenario by that name -- the only answer that means "do not ask"
;    0  the scenario wanted a tree and nothing here fits this pair
;    1  the scenario constrains nothing by design; AAF picks freely
;    2  something fits, but nothing guarantees it reaches a climax
;    3  a matching tree that is known to finish
;
; Only -1 is a refusal. 0 through 3 all PLAY -- they differ in how strong a
; promise Rapport can make about how the scene goes. At 0 and 1 AAF chooses
; the position itself and the scene may be short or end vaguely; Rapport still
; keeps the faces and applies the aftermath either way. So 0 is a reason to
; prefer a different scenario, never a reason to leave two actors standing.
;
; It answers with the selection the start would actually make, on this
; install, for these two actors, right now -- not a static capability check.
; Two women get 0 from every tree-bearing scenario on a stock install: AAF's
; packs ship 24 female/female positions and not one of them enters a position
; tree. They play; nothing can promise how they end.
Int Function CanRun(String asScenario, Actor akFirst, Actor akSecond) Global Native

; Then ask. False means not now -- a scene is already running, the bridge is
; not up yet, or one of the actors is None. All of those are transient, so
; treat a False as "try again later" rather than an error; the reason is in
; Rapport.log.
;
; Rapport takes it from here: it chooses the tree, keeps the faces, stops the
; scene when the tree ends, and applies the aftermath. You get SceneStarted
; and SceneEnded back through your own AAF listeners if you want them.
Bool Function RequestScene(Actor akFirst, Actor akSecond, String asScenario) Global Native
