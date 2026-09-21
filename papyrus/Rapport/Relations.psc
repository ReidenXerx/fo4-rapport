Scriptname Rapport:Relations Hidden
{The engine's side of a relationship, read from Papyrus because that is the only
 place an association can be asked about. Rapport's store (Rapport:Core.PairBond)
 holds what has HAPPENED between two people; these answer what the game says they
 ARE, and BondBetween joins the two for a consumer that has to rank pairs now.}

; The engine's associations (R-3): they cannot be enumerated, so the forms are
; hardcoded - Fallout4.esm ASTP records, read from the master. Decimal, because
; Papyrus has no hex literal parser.
Bool Function HasAssociationID(Actor akFirst, Actor akSecond, Int aiID) Global
	AssociationType kind = Game.GetFormFromFile(aiID, "Fallout4.esm") as AssociationType
	Return kind && akFirst.HasAssociation(kind, akSecond)
EndFunction

; BLOOD: Siblings 1996C, ParentChild 1996B, GrandparentGrandchild 19968, GrandAuntUncle
; 19967, Cousins 19963, AuntUncle 1995F. In-laws are not blood. The store keeps this as
; the INCEST flag - a flag, never a refusal (R-14).
Bool Function AreBloodRelated(Actor akFirst, Actor akSecond) Global
	Return Rapport:Relations.HasAssociationID(akFirst, akSecond, 104812) || Rapport:Relations.HasAssociationID(akFirst, akSecond, 104811) || Rapport:Relations.HasAssociationID(akFirst, akSecond, 104808) || Rapport:Relations.HasAssociationID(akFirst, akSecond, 104807) || Rapport:Relations.HasAssociationID(akFirst, akSecond, 104803) || Rapport:Relations.HasAssociationID(akFirst, akSecond, 104799)
EndFunction

; PARTNERS: Spouse 1996D, Courting 19962.
Bool Function ArePartners(Actor akFirst, Actor akSecond) Global
	Return Rapport:Relations.HasAssociationID(akFirst, akSecond, 104813) || Rapport:Relations.HasAssociationID(akFirst, akSecond, 104802)
EndFunction

; Married or courting ANYONE. HasAssociation with None asks "with anybody" - the
; engine's own convention, checked in game on a spouse and a bachelor before this was
; relied on (Chemistry's faithfulness cost depends on it).
Bool Function HasPartner(Actor akActor) Global
	If akActor == None
		Return False
	EndIf
	Return Rapport:Relations.HasAssociationID(akActor, None, 104813) || Rapport:Relations.HasAssociationID(akActor, None, 104802)
EndFunction

; The engine's rank between two people, the higher of the two directions: the store's
; key ignores order, so the seed must too, or it would depend on who came first.
Int Function RankBetween(Actor akFirst, Actor akSecond) Global
	Int ab = akFirst.GetRelationshipRank(akSecond)
	Int ba = akSecond.GetRelationshipRank(akFirst)
	If ba > ab
		Return ba
	EndIf
	Return ab
EndFunction

; The bond a consumer should rank this pair by, -1..+1. Rapport's stored bond once
; the pair has a record; before that, what the engine's relationship WOULD seed -
; without writing a record, because a pair is recorded on interaction, never on
; sight (R-5). Without this, a married couple who have never had a scene would read
; 0 and be no likelier than strangers to have their first.
Float Function BondBetween(Actor akFirst, Actor akSecond) Global
	If akFirst == None || akSecond == None
		Return 0.0
	EndIf
	Int first = akFirst.GetFormID()
	Int second = akSecond.GetFormID()
	If Rapport:Core.IsPairSeeded(first, second)
		Return Rapport:Core.PairBond(first, second)
	EndIf
	; Not seeded, but an addon may already have moved it by id (Rapport:Core.AddBond):
	; that movement sits on top of what the engine would have seeded.
	Return Rapport:Core.PreviewBond(first, second, Rapport:Relations.RankBetween(akFirst, akSecond), Rapport:Relations.ArePartners(akFirst, akSecond))
EndFunction

; The way an ADDON should change a bond when it holds both Actors (dialogue, a gift):
; imports the engine's relationship first, exactly as a scene does, so the pair's
; starting point is not lost. Returns the new bond. aiReason: 3 dialogue, 4 gift,
; 5 any other addon.
Float Function AddBondBetween(Actor akFirst, Actor akSecond, Float afAmount, Int aiReason) Global
	If akFirst == None || akSecond == None
		Return 0.0
	EndIf
	Int first = akFirst.GetFormID()
	Int second = akSecond.GetFormID()
	If !Rapport:Core.IsPairSeeded(first, second)
		Rapport:Core.NoteVanillaRelationship(first, second, Rapport:Relations.RankBetween(akFirst, akSecond), Rapport:Relations.AreBloodRelated(akFirst, akSecond), Rapport:Relations.ArePartners(akFirst, akSecond))
	EndIf
	Return Rapport:Core.AddBond(first, second, afAmount, aiReason)
EndFunction
