Scriptname BodyGen Native Hidden
{IMPORT ONLY - never compiled into the release. LooksMenu ships the real BodyGen.pex
 inside "LooksMenu - Main.ba2". Declared from LooksMenu's own registrations
 (f4ee/PapyrusBodyGen.cpp, RegisterFuncs), argument for argument, so the build does
 not depend on unpacking an archive.}

Function SetMorph(Actor akActor, Bool isFemale, String morph, Keyword akKeyword, Float value) Native Global
Float Function GetMorph(Actor akActor, Bool isFemale, String morph, Keyword akKeyword) Native Global
Function RemoveMorphsByName(Actor akActor, Bool isFemale, String morph) Native Global
Function RemoveMorphsByKeyword(Actor akActor, Bool isFemale, Keyword akKeyword) Native Global
Function RemoveAllMorphs(Actor akActor, Bool isFemale) Native Global
Keyword[] Function GetKeywords(Actor akActor, Bool isFemale, String morph) Native Global
String[] Function GetMorphs(Actor akActor, Bool isFemale) Native Global
; Clears the actor's morphs and runs BodyGen again; applies them when update is True.
Function RegenerateMorphs(Actor akActor, Bool update) Native Global
Function UpdateMorphs(Actor akActor) Native Global
Function ClearAll() Native Global
Bool Function SetSkinOverride(Actor akActor, String id) Native Global
Bool Function RemoveSkinOverride(Actor akActor) Native Global
