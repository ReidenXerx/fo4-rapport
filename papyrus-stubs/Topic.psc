; Reconstructed base sources are INCOMPLETE. This type exists in the game and is
; used by base scripts - three of them declare a Topic Property - but no source
; for it survives in the reconstruction, so the compiler rejects any script that
; names the type.
;
; These stubs live in their own IMPORT-ONLY directory, never in papyrus/, because
; build-papyrus compiles everything under papyrus/ with -all: a stub there would
; produce a Topic.pex that shadows the game's own type at runtime.
;
; Shape copied from the stubs that DID survive - AssociationType, Keyword, Scene
; are all "Extends Form Native hidden" with no functions.
ScriptName Topic Extends Form Native hidden
