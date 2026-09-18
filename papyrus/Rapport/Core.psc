Scriptname Rapport:Core Native Hidden
{Everything Papyrus can ask of Rapport.dll. Native functions have to live in a
 script flagged Native, which a Quest script cannot be, so they sit here and the
 bridge calls them by name.}

; The bridge has found AAF, or has not. Called once per session.
Function BridgeReady(Bool abAafPresent) Global Native

; A scene we asked for has begun.
Function SceneStarted(Int aiRequest) Global Native

; A scene we asked for has finished and its actors are released.
Function SceneEnded(Int aiRequest) Global Native

; A request never became a scene. asWhy is for the log, not for logic.
Function RequestFailed(Int aiRequest, String asWhy) Global Native

; One log for the whole mod. Papyrus writes into Rapport.log rather than into a
; second file that nobody thinks to read next to the first.
Function Trace(String asText) Global Native
