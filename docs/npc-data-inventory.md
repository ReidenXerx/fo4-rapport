<!-- Surveyed 2026-09-20 for the SEU-replacement design (a relationship / flirt /
     attraction system). Inventory only: what the engine exposes about an NPC and
     what it does not. Every entry is a direct file read with a line citation —
     none of these trees is in any code graph, so a graph zero about them means
     nothing. Spot-checked: Actor.psc:144/150/154/500 and AAF_API.psc:374/488 are
     exact, and Relationship.psc really is absent. -->

# Fallout 4 — what a mod can READ and WRITE about an NPC

Inventory only. No design, no ranking. Every line was opened; anything I did not open is marked MISSED.

Path shorthand used below:

- **P** = `D:\F4CustomMods\PapyrusBase\Source\Base\`
- **C** = `C:\Users\DuduPhudu\Documents\Projects\fo4-rapport\extern\CommonLibF4\CommonLibF4\include\RE\Bethesda\`
- **R** = `C:\Users\DuduPhudu\Documents\Projects\fo4-rapport\src\`
- **X** = `C:\Users\DuduPhudu\Documents\Projects\fo4-chemistry\papyrus\Chemistry\Autonomy.psc`

**CHECKED (global):** `ls` of P (1808 .psc files); full declaration grep of `Actor.psc` (694 lines), `ActorBase.psc`, `Faction.psc`, `Form.psc`, `ObjectReference.psc` (791), `Location.psc`, `Cell.psc`, `Game.psc`, `Quest.psc`, `ReferenceAlias.psc`, `Alias.psc`, `Scene.psc`, `ScriptObject.psc`, `Utility.psc`, `Package.psc`, `Keyword.psc`, `GlobalVariable.psc`, `FormList.psc`, `EncounterZone.psc`, `followersscript.psc` (1510), `companionactorscript.psc` (993), `AAF/AAF_API.psc`. `ls` of C (180 headers) plus targeted reads of `Actor.h`, `TESBoundAnimObjects.h` (TESNPC), `FormComponents.h`, `TESForms.h`, `TESFaction.h`, `TESObjectREFRs.h`, `ActorValueInfo.h`, `BSExtraData.h`, `Events.h`, `SCRIPT_OUTPUT.h`. Full reads of `R\ActorScan.cpp/.h`, `R\Pairing.cpp`, `R\Candidates.cpp`, plus greps of `R\PapyrusLink.cpp`, `R\Ledger.cpp/.h`, `R\Expressions.h`; and `X` plus `fo4-rapport\papyrus\Rapport\Bridge.psc` / `Core.psc`.

---

## 1. Identity & demographics

| Item | Where | R/W | Layer |
|---|---|---|---|
| Form ID | `P Form.psc:5` `Int Function GetFormID() Native` | R | Papyrus + C++ |
| Base object | `P Actor.psc:600` `ActorBase Function GetActorBase()` | R | Papyrus |
| Base object, template-resolved | `P Actor.psc:132` `ActorBase Function GetLeveledActorBase() Native` | R | Papyrus |
| Sex | `P ActorBase.psc:17` `Int Function GetSex() Native` | R | Papyrus |
| Sex (native) | `C TESBoundAnimObjects.h:492` `[[nodiscard]] SEX GetSex() noexcept`; enum `C TESBoundAnimObjects.h:382` `kNone=-1, kMale=0, kFemale=1` | R | C++ |
| Sex (base-data flag) | `C FormComponents.h:1586` `IsFemale() ... Flag::kFemale`; flag at `C FormComponents.h:1511` | R | C++ |
| Race | `P Actor.psc:142` `Race Function GetRace() Native`; `P ActorBase.psc:15` `Race Function GetRace() Native` | R | Papyrus |
| Race (field) | `C Actor.h:1354` `TESRace* race; // 418` | R/W | C++ |
| Race (set) | `P Actor.psc:498` `Function SetRace(Race akRace) Native` | W | Papyrus |
| Original race | `C TESBoundAnimObjects.h:538` `TESRace* originalRace; // 268` | R | C++ |
| Class | `P ActorBase.psc:5` `Class Function GetClass() Native`; `C TESBoundAnimObjects.h:533` `TESClass* cl; // 240` | R | both |
| Level (current) | `P Actor.psc:130` `Int Function GetLevel() Native`; `C Actor.h:1143` `[[nodiscard]] std::int16_t GetLevel()` | R | both |
| Level (base / exact) | `P ActorBase.psc:11` `GetLevel()`, `P ActorBase.psc:13` `Int Function GetLevelExact() Native` | R | Papyrus |
| Level fields | `C FormComponents.h:1553` `std::uint16_t level; // 06`, `calcLevelMin/Max` | R/W | C++ |
| Unique vs generic | `P ActorBase.psc:27` `Bool Function IsUnique() Native`; `C FormComponents.h:1592` `IsUnique()` / flag `kUnique = 1 << 5` (`C FormComponents.h:1516`) | R | both |
| Child | `P Actor.psc:186` `Bool Function IsChild() Native`; `C TESObjectREFRs.h:593` `virtual bool IsChild() const` | R | both |
| Essential / protected / invulnerable / ghost | `P Actor.psc:200,464,204,472`; `P ActorBase.psc:21,23,25,29,31,35`; `C FormComponents.h:1585,1589,1587,1593` | R+W | both |
| Template use flags | `C FormComponents.h:1599` `UsesTemplate()`; enum `TEMPLATE_USE_FLAG` at `C FormComponents.h:1526`-ish (kTraits/kStats/kFactions/kSpells/kAIData/kAIPackages/kBaseData/kInventory/kScript/kAIDefPackList/kAttackData/kKeywords) | R | C++ |
| Template forms | `C FormComponents.h:1613-1614` `TESForm* baseTemplateForm; TESForm** templateForms;` | R | C++ |
| Full name | `C TESObjectREFRs.h:750` `[[nodiscard]] const char* GetDisplayFullName()` — proven in `R PapyrusLink.cpp:547` | R | **C++ only** |
| Short name | `C TESBoundAnimObjects.h:548` `BGSLocalizedString shortName; // 298` | R | C++ |
| Voice type | `P ObjectReference.psc:201` `VoiceType Function GetVoiceType() Native`; override `P Actor.psc:488` `SetOverrideVoiceType(VoiceType)`; field `C FormComponents.h:1612` `BGSVoiceType* voiceType; // 28` | R/W | both |
| Height / morph weight | `C TESBoundAnimObjects.h:540-542` `NiPoint3 morphWeight; float height; float heightMax;` | R | C++ |
| Base disposition (editor) | `C FormComponents.h:1556` `std::uint16_t baseDisposition; // 0C` | R | C++ |

**MISSED** — "age" has no representation anywhere I opened. `IsChild()` is the only age-ish signal; there is no birth year, no adult/elder flag. Nothing named age exists in `Actor.psc`, `ActorBase.psc`, `TESNPC`, `ACTOR_BASE_DATA`.

**NEGATIVE (verified):** `P Form.psc` (17 lines, read whole) has **no `GetName`**. `P ObjectReference.psc` has no `GetDisplayName`. `P Race.psc`, `P Class.psc`, `P ActorValue.psc`, `P AssociationType.psc`, `P Outfit.psc`, `P Armor.psc`, `P HeadPart.psc`, `P CombatStyle.psc`, `P Idle.psc`, `P LocationRefType.psc` are all **1-line `ScriptName … Native hidden` stubs with zero functions.** `X:526-528` states this in the shipping code: *"Fallout 4's base Papyrus has no way to ask for one: no GetName on Form, no GetDisplayName on ObjectReference, nothing on Actor."* The name must come from a plugin (`R PapyrusLink.cpp:542` `Papyrus_ActorName`).

---

## 2. Relationship & disposition

| Item | Where | R/W | Layer |
|---|---|---|---|
| Rank to another actor | `P Actor.psc:144` `Int Function GetRelationshipRank(Actor akOther) Native` | R | Papyrus |
| Set rank | `P Actor.psc:500` `Function SetRelationshipRank(Actor akOther, Int aiRank) Native` | W | Papyrus |
| Highest / lowest rank held | `P Actor.psc:126` `Int Function GetHighestRelationshipRank() Native`; `P Actor.psc:136` `GetLowestRelationshipRank()` | R | Papyrus |
| Family relationship | `P Actor.psc:154` `Bool Function HasFamilyRelationship(Actor akOther) Native` | R | Papyrus |
| Parent relationship | `P Actor.psc:160` `Bool Function HasParentRelationship(Actor akOther) Native` | R | Papyrus |
| Named association | `P Actor.psc:150` `Bool Function HasAssociation(AssociationType akAssociation, Actor akOther) Native` | R | Papyrus |
| Relationship-change story event | `P Quest.psc:188` `Function OnStoryRelationshipChange(ObjectReference akActor1, ObjectReference akActor2, Int aiOldRelationship, Int aiNewRelationship)` | R (event) | Papyrus |
| Rank carried on a kill event | `P Quest.psc:156` `OnStoryKillActor(..., Int aiCrimeStatus, Int aiRelationshipRank)` | R (event) | Papyrus |
| `BGSRelationship` form | `C TESForms.h:2881-2895` — `TESNPC* npc1; // 20`, `TESNPC* npc2; // 28`, `BGSAssociationType* assocType; // 30`, `std::uint32_t packedData; // 38` | R/W | **C++ only** |
| Relationship list on an NPC | `C TESBoundAnimObjects.h:564` `BSTArray<BGSRelationship*>* relationships; // 2F0` | R/W | **C++ only** |
| `BGSAssociationType` | `C TESForms.h:2940-2952` — `BSFixedStringCS associationLabel[2][2]; // 20`, `std::uint32_t flags; // 40` | R | **C++ only** |
| Condition-function IDs (what dialogue conditions can test) | `C SCRIPT_OUTPUT.h:956` `FUNCTION_GET_RELATIONSHIP_RANK = 0x1193`, `:957` `SET_RELATIONSHIP_RANK`, `:1168-1171` `GET_HIGHEST/LOWEST_RELATIONSHIP_RANK`, `HAS_ASSOCIATION_TYPE_ANY`, `HAS_FAMILY_ANY`, `:811-814` `HAS_ASSOCIATION_TYPE_ID`, `HAS_FAMILY_RELATIONSHIP`, `HAS_PARENT_RELATIONSHIP` | — | C++ (enum) |
| Favor / friend state conditions | `C SCRIPT_OUTPUT.h:1187-1191` `SET_FAVOR_STATE`, `IS_IN_FAVOR_STATE`, `IS_IN_FRIEND_STATE` | — | C++ (enum) |
| Doing a favor | `P Actor.psc:196` `Bool Function IsDoingFavor() Native`; `P Actor.psc:462` `Function SetDoingFavor(Bool abDoingFavor, Bool abWorkShopMode) Native` | R/W | Papyrus |
| Angry-with-player flag | `C Actor.h:882` `kAngryWithPlayer = 1 << 11` (in `BOOL_FLAGS`, `C Actor.h:868`) | R/W | **C++ only** |
| Bribed / intimidated | `P Actor.psc:184,450,220,478`; flags `C Actor.h:881` `kBribedByPlayer`; `P Actor.psc:96` `GetBribeAmount()`, `:544` `WillIntimidateSucceed()`; timestamp `C Actor.h:1347` `std::uint32_t intimidateBribeDayStamp; // 3A8` | R/W | both |

**NEGATIVE (verified):** there is **no `Relationship.psc`** and no `Spousal`/`Marriage` script — `find`/`ls` over P returned nothing. `AssociationType.psc` is a 1-line stub, so you can hold an `AssociationType` property and pass it to `HasAssociation`, and that is the entire Papyrus surface: **you cannot enumerate, read the label of, or create an association from Papyrus.** `BGSRelationship` is reachable **only from C++**, and only as the *editor-authored* RELA record; `packedData` is unparsed in the header (no accessor, no bit documentation).

**MISSED** — where a **runtime** `SetRelationshipRank` is stored. `C BSExtraData.h:123` lists `kFactionChanges` in the EXTRA_DATA_TYPE enum but **`ExtraFactionChanges` has no class definition anywhere in C** (grep found only the enum entry). I could not find an extra-data class for relationship changes either. So: a runtime relationship change is readable from Papyrus via `GetRelationshipRank`, but I did **not** establish a C++ read path for it.

**MISSED** — the integer meaning of the rank scale. Neither `Actor.psc` nor any header I opened declares the enum (Lover/Ally/Friend/Acquaintance/Rival/Foe/Enemy/Archnemesis is the Skyrim convention, and I did not verify it here).

---

## 3. Companion / affinity

The companion affinity system is **entirely Papyrus + ActorValues + a quest script**; no C++ surface.

| Item | Where | R/W |
|---|---|---|
| The affinity number | `P followersscript.psc:154` `ActorValue Property CA_Affinity Auto Const` — read via `Self.GetValue(Followers.CA_Affinity)` at `P companionactorscript.psc:225`, written at `:516-517` | R/W |
| Current / highest / lowest threshold | `P followersscript.psc:155,172,173` `CA_CurrentThreshold`, `CA_HighestThreshold`, `CA_LowestThreshold` | R/W |
| Highest / lowest ever reached | `P followersscript.psc:175,176` `CA_HighestReached`, `CA_LowestReached` | R/W |
| Wants to talk | `P followersscript.psc:156` `CA_WantsToTalk`; `:161` `CA_WantsToTalkRomanceRetry` | R/W |
| Romance flags | `P followersscript.psc:185,186` `CA_IsRomantic`, `CA_IsRomanceableNow`; helpers `P companionactorscript.psc:401` `Bool Function IsInfatuated()`, `:405` `Bool Function IsRomantic()` | R |
| Last change direction | `P followersscript.psc:174` `CA_LastChangePositive` | R/W |
| Affinity scene to play | `P followersscript.psc:170` `CA_AffinitySceneToPlay` | R/W |
| Murder bookkeeping | `P followersscript.psc:177,179,181` `CA_MurderSessionCount`, `CA_MurderSessionDay`, `CA_MurderSessionVictimCount` | R/W |
| Custom slot | `P followersscript.psc:171` `ActorValue Property CA_Custom Auto Const` | R/W |
| Temporary anger | `P companionactorscript.psc:187` `ActorValue Property TemporaryAngerLevel Auto Const mandatory` | R/W |
| Fire an affinity event | `P followersscript.psc:999` `Bool Function SendAffinityEvent(ScriptObject Sender, Keyword EventKeyword, ObjectReference target, GlobalVariable EventSizeOverride, Bool CheckCompanionProximity, Bool ShouldSuppressComment, Bool IsDialogueBump, Float ResponseDelay) Global` | W |
| Subscribe to affinity events | `P companionactorscript.psc:256` `RegisterForCustomEvent(Followers as ScriptObject, "followersscript_AffinityEvent")`; handler `:294` | R |
| The generic four | `P followersscript.psc:142-149` `DislikesEvent`, `HatesEvent`, `LikesEvent`, `LovesEvent` (Keywords) | — |
| Event sizes | `P followersscript.psc:187-189` `SmallAffinityEvent`, `NormalAffinityEvent`, `LargeAffinityEvent` (Globals) | R |
| Event definition struct | `P followersscript.psc:8-26` `Struct AffinityEventData { Keyword EventKeyword; GlobalVariable EventSize; GlobalVariable CoolDownDays; Keyword TopicSubType; ActorValue AssociatedActorValue; Float NextDayAllowed }` | — |
| Trait preferences | `P companionactorscript.psc:135` `TraitPreference_Array`; applied `:473` `Function TryToSetTraitValues(ActorValue TraitToFind, GlobalVariable EventSize, Bool ShouldAlsoSetAffinity)` | R/W |
| Bounds | `P companionactorscript.psc:131,133` `MinAffinity = -1100.0`, `MaxAffinity = 1100.0` | — |
| Passive gain while travelling | `P companionactorscript.psc:105-106` `WithPlayerBaseAffinityGainPerTick = 40.0`, `WithPlayerCurrentAffinityMult = 0.033`; `:223 GetWithPlayerAffinityGain()` | — |
| Companion factions | `P followersscript.psc:287-289` `CurrentCompanionFaction`, `HasBeenCompanionFaction`, `DisallowedCompanionFaction` | R |
| Companion control | `P Actor.psc:670` `SetCompanion(Bool, Bool)`, `:675 SetAvailableToBeCompanion()`, `:680 DisallowCompanion(Bool)`, `:685 AllowCompanion(Bool, Bool)` | W |
| Favor points | `P Actor.psc:552` `ModFavorPoints(Int)`, `:558 ModFavorPointsWithGlobal(GlobalVariable)` | W |
| Dismiss | `P Actor.psc:276` `Event OnCompanionDismiss()`; `P companionactorscript.psc:378` handler | R (event) |
| Player followers | `P Game.psc:71` `Actor[] Function GetPlayerFollowers() Global Native` | R |
| Teammate | `P Actor.psc:226` `IsPlayerTeammate()`, `:494 SetPlayerTeammate(Bool abTeammate, Bool abCanDoFavor, Bool abGivePlayerXP)`; flag `C Actor.h:878` `kCanDoFavor = 1 << 7` | R/W |

**NOTE (semantic):** affinity is per-companion and keyed to the **player only**. Nothing in `followersscript.psc` or `companionactorscript.psc` models NPC-to-NPC affinity. The `Followers` quest is the only publisher of `followersscript_AffinityEvent`, so a flirt system riding this channel is riding the vanilla companion roster.

---

## 4. Factions

| Item | Where | R/W | Layer |
|---|---|---|---|
| Membership | `P Actor.psc:212` `Bool Function IsInFaction(Faction akFaction) Native` | R | Papyrus |
| Rank | `P Actor.psc:116` `Int Function GetFactionRank(Faction akFaction) Native`; write `:468 SetFactionRank(Faction, Int)`, `:252 ModFactionRank(Faction, Int)` | R/W | Papyrus |
| Join / leave | `P Actor.psc:690` `AddToFaction(Faction)`, `:426 RemoveFromFaction(Faction)`, `:424 RemoveFromAllFactions()` | W | Papyrus |
| Reaction between two actors | `P Actor.psc:118` `Int Function GetFactionReaction(Actor akOther) Native`; `P Faction.psc:13` `Int Function GetFactionReaction(Actor akOther) Native` | R | Papyrus |
| Hostility | `P Actor.psc:208` `Bool Function IsHostileToActor(Actor akActor) Native`; native `C Actor.h:1129` `[[nodiscard]] bool GetHostileToActor(Actor* a_actor)` | R | both |
| Ally / enemy authoring | `P Faction.psc:39` `SetAlly(Faction akOther, Bool abSelfIsFriendToOther, Bool abOtherIsFriendToSelf)`; `:45 SetEnemy(...)` | W | Papyrus |
| Player enemy / expelled | `P Faction.psc:27,29,47,49` `IsPlayerEnemy()`, `IsPlayerExpelled()`, `SetPlayerEnemy(Bool)`, `SetPlayerExpelled(Bool)` | R/W | Papyrus |
| Crime faction | `P Actor.psc:102` `Faction Function GetCrimeFaction() Native`, `:458 SetCrimeFaction(Faction)`; field `C TESBoundAnimObjects.h:554` `TESFaction* crimeFaction; // 2C8` | R/W | both |
| Crime gold | `P Faction.psc:7,9,11,31,41,43` `GetCrimeGold`, `…NonViolent`, `…Violent`, `ModCrimeGold(Int, Bool)`, `SetCrimeGold(Int)`, `SetCrimeGoldViolent(Int)` | R/W | Papyrus |
| Infamy | `P Faction.psc:15,17,19` `GetInfamy()`, `GetInfamyNonViolent()`, `GetInfamyViolent()` | R | Papyrus |
| Crime group membership | `P Faction.psc:25` `Bool Function IsFactionInCrimeGroup(Faction akOther) Native` | R | Papyrus |
| Base faction list (cheap) | `C FormComponents.h:1617` `BSTArray<FACTION_RANK> factions; // 50`; `FACTION_RANK` at `C FormComponents.h:1562` = `{ TESFaction* faction; std::int8_t rank; }` — used in production at `R Pairing.cpp:24-33` | R/W | C++ |
| NPC membership helper | `C TESBoundAnimObjects.h:511` `[[nodiscard]] bool IsInFaction(const TESFaction* a_faction)` (loops `factions`, `rank > -1`) | R | C++ |
| Actor membership (runtime-aware) | `C Actor.h:984` `virtual bool IsInFaction(const TESFaction* a_faction) const; // 10A` | R | C++ |
| Faction reaction table | `C FormComponents.h:1861` `TESReactionForm` → `:1869` `BSSimpleList<GROUP_REACTION*> reactionList; // 08`; `GROUP_REACTION` at `C FormComponents.h:1855` = `{ TESForm* form; std::int32_t reaction; FIGHT_REACTION fightReaction; }`; `TESFaction` inherits it at `C TESFaction.h:91` | R/W | **C++ only** |
| Faction crime data | `C TESFaction.h:22` `FACTION_CRIME_DATA_VALUES { bool arrest; bool attackOnSight; std::uint16_t murderCrimeGold; assaultCrimeGold; trespassCrimeGold; pickpocketCrimeGold; float stealCrimeGoldMult; std::uint16_t escapeCrimeGold; }` | R/W | C++ |
| Faction vendor data | `C TESFaction.h:52` `{ startHour; endHour; locationRadius; buysStolen; notBuySell; buysNonStolen }` | R | C++ |
| Rank titles | `C TESFaction.h:78` `RANK_DATA { BGSLocalizedString maleRankTitle; femaleRankTitle; TESTexture textureInsignia; }`; list at `C TESFaction.h:103` | R | **C++ only** |
| Per-NPC crime gold | `C TESFaction.h:99` `BSTHashMap<const TESNPC*, std::uint32_t>* crimeGoldMap; // 50` | R | C++ |
| Vendor faction on an actor | `C Actor.h:1332` `TESFaction* vendorFaction; // 330` | R | C++ |

**NEGATIVE (verified):** `R Pairing.cpp:21-23` documents the gap in shipping code — *"Base faction lists only. Runtime faction changes live in extra data and are not consulted."* Confirmed: `C BSExtraData.h:123` has `kFactionChanges` in the enum, and grep across all of C finds **no `ExtraFactionChanges` class**. So in C++ the cheap `npc->factions` read is *base only*; a runtime `AddToFaction` is invisible to it. Papyrus `IsInFaction` / `Actor::IsInFaction` (vtable 0x10A) do see runtime changes.

**NEGATIVE:** `P Faction.psc` has **no rank-title accessor and no member enumeration**. You cannot ask "who is in this faction" from Papyrus.

---

## 5. Actor values

**How to read/write (Papyrus).** All AV access is on `ObjectReference`, not `Actor`:

- `P ObjectReference.psc:197` `Float Function GetValue(ActorValue akAV) Native`
- `P ObjectReference.psc:125` `Float Function GetBaseValue(ActorValue akAV) Native`
- `P ObjectReference.psc:199` `Float Function GetValuePercentage(ActorValue akAV) Native`
- `P ObjectReference.psc:595` `Function SetValue(ActorValue akAV, Float afValue) Native`
- `P ObjectReference.psc:287` `Function ModValue(ActorValue akAV, Float afAmount) Native`
- `P ObjectReference.psc:529` `Function RestoreValue(ActorValue akAV, Float afAmount) Native`
- `P ObjectReference.psc:71` `Function DamageValue(ActorValue akAV, Float afDamage) Native`
- `P ObjectReference.psc:183` `Float Function GetResourceDamage(ActorValue akValue) Native`
- Alias-safe wrappers: `P ReferenceAlias.psc:467,475,483,492` `TryToGetActorValue`, `TryToGetValue`, `TryToSetActorValue`, `TryToSetValue`

**How to read/write (C++).** `C FormComponents.h:152` `ActorValueOwner`:
- `:161` `virtual float GetActorValue(const ActorValueInfo&) const` — 01
- `:162` `GetPermanentActorValue` — 02
- `:163` `GetBaseActorValue` — 03
- `:164` `SetBaseActorValue(const ActorValueInfo&, float)` — 04
- `:165` `ModBaseActorValue(…, float a_delta)` — 05
- `:166` `ModActorValue(ACTOR_VALUE_MODIFIER, …, float)` — 06
- `:167` `GetModifier(ACTOR_VALUE_MODIFIER, …)` — 07
- `:168` `RestoreActorValue(…, float)` — 08
- `:169` `SetActorValue(const ActorValueInfo&, float)` — 09

Runtime storage: `C Actor.h:1333` `ActorValueStorage avStorage; // 338`, defined at `C Actor.h:829` as `BSTArray<BSTTuple<std::uint32_t,float>> baseValues; BSTArray<BSTTuple<std::uint32_t,Modifiers>> modifiers; BSReadWriteLock avLock;`.

**Getting an AV form from Papyrus without a property:** `P Game.psc:39-84` — `GetAggressionAV()`, `GetAgilityAV()`, `GetCharismaAV()`, `GetConfidenceAV()`, `GetEnduranceAV()`, `GetHealthAV()`, `GetIntelligenceAV()`, `GetLuckAV()`, `GetPerceptionAV()`, `GetStrengthAV()`, `GetSuspiciousAV()` — all `Global Native`, all returning `ActorValue`.

**The AV registry (C++).** `C ActorValueInfo.h:14` `class ActorValue : public BSTSingletonImplicit<ActorValue>`, singleton at `C ActorValueInfo.h:58`. Named pointers relevant to a social system, with offsets:

- `aggression // 010`, `assistance // 040`, `confidence // 098`, `morality // 158` (`C ActorValueInfo.h:68,74,85,109`)
- `charisma // 088`, `intelligence // 100`, `perception // 170`, `luck // 140`, `strength`, `endurance`, `agility` (`:83,98,112,106,…`)
- `suspicious // 250`, `waitingForPlayer // 278`, `ignorePlayerWhileFrenzied // 418` (`:…`, `C ActorValueInfo.h:187,190,198` region)
- `speechcraft // 230`, `sneak // 228`
- `karma // 110` (`C ActorValueInfo.h:100`) — present as a form; I did **not** verify it is used by FO4
- `idleChatterTimeMin // 0E8`, `idleChatterTimeMAx // 0F0` (`C ActorValueInfo.h:95-96`) — note the vanilla typo `MAx`
- `energy // 310`, `fatigue // 1E0`, `rads // 1D0`, `health // 0D8`, `stamina // 240`
- Settlement resources: `resourceHappiness // 378`, `resourceFood`, `resourceWater`, `resourceSafety`, `resourceBed` (`C ActorValueInfo.h:106-110` region)
- Bulk lists: `C ActorValueInfo.h:198` `BSTArray<ActorValueInfo*> hardcodedActorValues; // 420` and `:199` `conditionActorValues; // 438`

**Whether an AV persists — this is on the AV form itself.** `C ActorValueInfo.h:204` `class ActorValueInfo : public TESForm, TESFullName, TESDescription`, with `:226` `REX::EnumSet<ActorValue::AVType,std::int32_t> avType; // 1B0`, `:225`-region `flags; // 1AC`, `:231` `float defaultValue; // 1C4`, `formEditorID; // 088`, `abbreviation; // 1A0`.
The flags that decide persistence/clamping are at `C ActorValueInfo.h:32-56`: `kNoScriptModAV = 1<<3`, `kCache = 1<<4`, `kCachePermenant = 1<<5`, `kAlwaysClamp = 1<<6`, `kImplicitBase0/1/100 = 1<<10/11/12`, `kUserDefined = 1<<13`, `kDerived = 1<<15`, `kMin1 = 1<<20`, `kMax10 = 1<<21`, `kMax100 = 1<<22`, `kScaleBy100 = 1<<23`, `kPercentage = 1<<24`, `kDoesNotRecover = 1<<28`, `kHardcoded = 1<<31`, `kDepreciated = 1<<1`.
`AVType` enum at `C ActorValueInfo.h:18`: `kDerivedAttribute, kAttribute, kSkill, kAIAttribute, kResistance, kCondition, kCharge, kIntValue, kVariable, kResource`.

**Editor AI data (the four "personality" values, as packed bitfields):** `C FormComponents.h:1621` `AIDATA_GAME` — `:1625` `aggression: 2`, `:1626` `confidence: 3`, `:1627` `energy: 8`, `:1628` `morality: 2`, `:1630` `assistance: 2`, `useAggroRadius: 1`, `aggroRadius[3]`. Held at `C FormComponents.h:1653` `AIDATA_GAME aiData; // 08` inside `TESAIForm` (`C FormComponents.h:1645`), which `TESActorBase` inherits (`C TESBoundAnimObjects.h:339`).

**Proof a custom AV persists in the save:** `P companionactorscript.psc:244` `Self.setValue(Followers.CA_Affinity, LastThresholdCrossed.GetValue())` and `:516` `PreviousAffinity = Self.GetValue(Followers.CA_Affinity)` — `CA_Affinity` is a mod-authored (well, Bethesda-authored, but non-hardcoded) AV used exactly as durable per-actor storage across the whole companion system.

**MISSED** — I did **not** verify which specific AVs survive a save versus being recomputed. `kCachePermenant` and `kDoesNotRecover` are the flags that look like they decide it, but I found no code in the files I opened that reads them, so I will not claim a mapping.

**NEGATIVE (verified):** there is no `ActorValues.h` in C (`ls` of 180 headers). The AV surface is `ActorValueInfo.h` only. `P ActorValue.psc` is a 1-line stub — **zero functions**, so an `ActorValue` in Papyrus is an opaque handle you can only pass around.

---

## 6. State & behaviour

### Papyrus (all on `Actor.psc` unless noted)

| Signal | Line |
|---|---|
| `Int Function GetCombatState() Native` | `:98` |
| `Actor Function GetCombatTarget() Native` / `Actor[] Function GetAllCombatTargets() Native` | `:100` / `:94` |
| `Bool Function IsInCombat() Native` | `:210` |
| `Function StartCombat(Actor akTarget, Bool abPreferredTarget) Native` / `StopCombat()` / `StopCombatAlarm()` | `:516` / `:526` / `:528` |
| `Bool Function IsSneaking() Native` / `Function StartSneaking() Native` | `:234` / `:522` |
| `Int Function GetSitState() Native` | `:146` |
| `Int Function GetSleepState() Native` | `:148` |
| `Bool Function IsTalking() Native` | `:238` |
| `Actor Function GetDialogueTarget() Native` | `:106` |
| `Function AllowPCDialogue(Bool abTalk) Native` / `AllowBleedoutDialogue(Bool)` | `:20` / `:18` |
| `Bool Function IsInScene() Native` | `:218` |
| `Package Function GetCurrentPackage() Native` | `:104` |
| `Function EvaluatePackage(Bool abResetAI) Native` | `:74` |
| `Function MoveToPackageLocation() Native` | `:254` |
| `Bool Function IsDead() Native` / `IsBleedingOut()` / `IsUnconscious()` | `:190` / `:182` / `:242` |
| `Bool Function IsAlarmed()` / `IsAlerted()` / `Function SetAlert(Bool)` | `:168` / `:170` / `:440` |
| `Bool Function IsDetectedBy(Actor akOther) Native` / `HasDetectionLOS(ObjectReference) Native` | `:192` / `:152` |
| `Float Function GetLightLevel() Native` | `:134` |
| `Bool Function IsRunning()` / `IsSprinting()` / `IsWeaponDrawn()` / `IsInIronSights()` | `:230` / `:236` / `:244` / `:214` |
| `Bool Function IsGuard()` / `IsArrested()` / `IsTrespassing()` / `IsCommandedActor()` | `:206` / `:174` / `:240` / `:188` |
| `Function EnableAI(Bool abEnable, Bool abPauseVoice) Native` / `Bool Function IsAIEnabled()` | `:66` / `:166` |
| `Function SetHeadTracking(Bool abEnable) Native` / `SetLookAt(ObjectReference akTarget, Bool abPathingLookAt) Native` / `ClearLookAt()` | `:476` / `:480` / `:48` |
| `Bool Function PlayIdle(Idle akIdle) Native` / `PlayIdleWithTarget(Idle, ObjectReference) Native` / `PlayIdleAction(Action, ObjectReference) Native` | `:416` / `:420` / `:418` |
| `Bool Function SnapIntoInteraction(ObjectReference akTarget) Native` | `:512` |
| `Bool Function PathToReference(ObjectReference aTarget, Float afWalkRunPercent) Native` | `:414` |
| Anim archetype (mood-ish, writable) | `:626-666` `SetAnimArchetypeConfident/Depressed/Elderly/Friendly/Irritated/Neutral/Nervous`; raw `:32` `ChangeAnimArchetype(Keyword)`, `:34` `ChangeAnimFaceArchetype(Keyword)`, `:36` `ChangeAnimFlavor(Keyword)` |
| Expression override | `:42` `Function ClearExpressionOverride() Native` |
| Speech challenge | `:408` `Event OnSpeechChallengeAvailable(ObjectReference akSpeaker)`; `P Quest.psc:210` `ResetSpeechChallenges()` |
| Say a topic | `P ObjectReference.psc:535` `Function Say(Topic akTopicToSay, Actor akActorToSpeakAs, Bool abSpeakInPlayersHead, ObjectReference akTarget) Native`; `:537 SayCustom(Keyword akKeywordToSay, …)` |
| In dialogue with the player (ref-level) | `P ObjectReference.psc:257` `Bool Function IsInDialogueWithPlayer() Native` |
| Current scene (ref-level) | `P ObjectReference.psc:135` `Scene Function GetCurrentScene() Native` |
| Scene control | `P Scene.psc:11` `Bool Function IsPlaying() Native`, `:5 ForceStart()`, `:35 Start()`, `:37 Stop()`, `:33 Pause(Bool)`, `:9 IsActionComplete(Int)` |
| Restrain / unconscious | `:502` `Bool Function SetRestrained(Bool) Native`, `:506 SetUnconscious(Bool)` |

### C++

| Signal | Where |
|---|---|
| `talkingToPlayer` bitfield | `C Actor.h:788` `std::uint32_t talkingToPlayer: 1; // 08:28` (in `ActorState`, `C Actor.h:765`) |
| `lifeState` / `knockState` / `moveMode` / `flyState` / `meleeAttackState` | `C Actor.h:783-787` |
| `forceRun` / `forceSneak` / `headTracking` | `C Actor.h:789-791` |
| `weaponState` / `gunState` / `stance` / `staggered` / `recoil` | `C Actor.h:793,801,800,798,796` |
| `interactingState` (`INTERACTING_STATE` enum `C Actor.h:757`) | `C Actor.h:802` |
| `inSyncAnim` | `C Actor.h:804` |
| `SIT_SLEEP_STATE` accessors | `C Actor.h:775-776` `virtual bool DoSetSitSleepState(SIT_SLEEP_STATE) = 0; virtual SIT_SLEEP_STATE DoGetSitSleepState() const = 0;` |
| `BOOL_FLAGS` (32 flags) | `C Actor.h:868-902` — notably `kScenePackage`(0), **`kInRandomScene`(4)**, `kCanDoFavor`(7), `kCanSpeak`(13), `kBribedByPlayer`(10), `kAngryWithPlayer`(11), `kAttackOnSight`(15), `kIsCommandedActor`(16), `kIsTresspassing`(12), `kCrimeSearch`(24), `kDoNotShowOnStealthMeter`(26), `kMovementBlocked`(27), `kUnderwater`(31). Field: `C Actor.h:1359` `REX::EnumSet<BOOL_FLAGS,std::uint32_t> boolFlags; // 43C` |
| Running package | `C Actor.h:621` `TESPackage* GetPackageThatIsRunning()` (on `AIProcess`) |
| Current package struct | `C Actor.h:639` `ActorPackage currentPackage; // 18` |
| Occupied furniture | `C Actor.h:539` `[[nodiscard]] ObjectRefHandle GetOccupiedFurniture()` |
| Follow target / target / patrol / idle target | `C Actor.h:654-661` `followTarget // B8`, `target // BC`, `patrolLocation // D4`, `idleTarget // D8` |
| Currently speaking topic | `C Actor.h:659` `std::uint32_t currentSpeakingTopicID; // D0` |
| Greet | `C Actor.h:565` `bool ProcessGreet(Actor*, DIALOGUE_TYPE, DIALOGUE_SUBTYPE, TESObjectREFR* a_target, BGSDialogueBranch*, bool a_forceSub, bool a_stop, bool a_que, bool a_sayCallback)` |
| Start / end dialogue | `C Actor.h:963` `virtual bool InitiateDialogue(Actor* a_target, PackageLocation*, PackageLocation*); // 0F5`; `:964 EndDialogue(); // 0F6`; `:940 SetInDialoguewithPlayer(bool); // 0DE` |
| Combat group | `C Actor.h:960` `virtual CombatGroup* GetCombatGroup() const; // 0F2` |
| Combat target / killer | `C Actor.h:1337-1338` `ActorHandle currentCombatTarget; // 380`, `ActorHandle myKiller; // 384` |
| Dialogue item target | `C Actor.h:1336` `ObjectRefHandle dialogueItemTarget; // 37C` |
| Speaking done | `C Actor.h:991-992` `virtual bool QSpeakingDone() const; // 111`, `SetSpeakingDone(bool); // 112` |
| Voice timers | `C Actor.h:1340-1341` `float voiceTimer; // 38C`, `float voiceLengthTotal; // 390` |
| Head-track anim archetype | `C Actor.h:996` `virtual BGSKeyword* GetSpeakingAnimArchType() { return speakingAnimArchType; }`; field `C Actor.h:1328` |
| Sneak (native) | `C Actor.h:1006` `virtual bool SetSneaking(bool a_sneaking); // 120`; read `C Actor.h:1236` `bool IsSneaking()` |
| Alive/dead | `C TESObjectREFRs.h:659` `virtual bool IsDead(bool a_notEssential) const; // C0` |
| Talking (ref-level) | `C TESObjectREFRs.h:545` `virtual bool IsTalking() const; // 4E` |
| Health % | `C Actor.h:1122` `float GetHealthPercent()` |
| Visibility | `C Actor.h:1243` `bool IsVisible() const`; `ACTOR_VISIBILITY_MASK` `C Actor.h:839` |
| Quest-alias instancing (a proven "is this actor busy in a quest" probe) | `C BSExtraData.h:591` `class ExtraAliasInstanceArray`, type `:168 kAliasInstanceArray`; used at `R ActorScan.cpp:17-26` — `instance.instancedPackages && !instance.instancedPackages->empty()` |
| Process-list membership | `R ActorScan.cpp:70-71` `lists->highActorHandles`, `lists->middleHighActorHandles` (`C ProcessLists.h`) |

**Proven-in-practice probe block** — `fo4-rapport\papyrus\Rapport\Bridge.psc:842-853` reads, in one go on a live actor: `IsInScene()`, `IsInCombat()`, `IsTalking()`, `IsWeaponDrawn()`, `IsSneaking()`, `IsDead()`, `IsUnconscious()`, `GetSitState()`, `GetSleepState()`, `GetRelationshipRank(pc)`, `pc.GetHeadingAngle(subject)`. That is a working Papyrus state read of an arbitrary NPC.

**Semantic note, load-bearing.** `R ActorScan.cpp:112-129` documents from live testing that **`talkingToPlayer` reads 0 through an NPC-to-NPC ambient conversation** — it is about the *player*. The flag that catches two NPCs talking to each other is `BOOL_FLAGS::kInRandomScene` (`C Actor.h:875`), and the comment records the exact in-game incident (Goodneighbor Neighborhood Watch, 2026-09-20). `GetCurrentScene()` was rejected there as too broad because it also catches everyone standing in an authored quest scene.

**MISSED** — the integer meanings of `GetCombatState()`, `GetSitState()`, `GetSleepState()`, `GetFactionReaction()`, `GetEquippedItemType()`, `WouldRefuseCommand()`. `Actor.psc` declares them `Native` with no enum and no comment (the whole file is decompiled and carries no docstrings). `SIT_SLEEP_STATE` is named in `C Actor.h:775` but its enum body is **above line 740** and I did not open that range.

---

## 7. Location & ownership

| Item | Where | R/W | Layer |
|---|---|---|---|
| Current location | `P ObjectReference.psc:133` `Location Function GetCurrentLocation() Native`; `C TESObjectREFRs.h:743` `[[nodiscard]] BGSLocation* GetCurrentLocation() const` | R | both |
| Editor location | `P ObjectReference.psc:137` `Location Function GetEditorLocation() Native`; `C TESObjectREFRs.h:543` `virtual BGSLocation* GetEditorLocation() const; // 4C`; field `C Actor.h:1325` `BGSLocation* editorLocation; // 2F8` | R | both |
| Location change event | `P Actor.psc:336` `Event OnLocationChange(Location akOldLoc, Location akNewLoc)` | R | Papyrus |
| Parent cell | `P ObjectReference.psc:169` `Cell Function GetParentCell() Native`; `C TESObjectREFRs.h:766` `[[nodiscard]] TESObjectCELL* GetParentCell() const noexcept { return parentCell; }`; field `:952` | R | both |
| Interior? | `P Cell.psc:13` `Bool Function IsInterior() Native`; `C TESForms.h:1487` `Flag::kInterior = 1u << 0` | R | both |
| Cell actor owner | `P Cell.psc:7` `ActorBase Function GetActorOwner() Native`; write `:19 SetActorOwner(ActorBase)` | R/W | Papyrus |
| Cell faction owner | `P Cell.psc:9` `Faction Function GetFactionOwner() Native`; write `:21 SetFactionOwner(Faction)` | R/W | Papyrus |
| Ref actor owner | `P ObjectReference.psc:105` `ActorBase Function GetActorOwner() Native`; `:107 GetActorRefOwner()`; `:213 HasActorRefOwner()`; writes `:545 SetActorOwner(ActorBase, Bool abNoCrime)`, `:547 SetActorRefOwner(Actor, Bool abNoCrime)` | R/W | Papyrus |
| Ref faction owner | `P ObjectReference.psc:141` `GetFactionOwner()`, `:565 SetFactionOwner(Faction, Bool abNoCrime)` | R/W | Papyrus |
| Ownership tests | `P ObjectReference.psc:265` `Bool Function IsOwnedBy(Actor akOwner) Native`; `P Actor.psc:608` `Bool Function IsOwner(ObjectReference akObject)`; `:548 WouldBeStealing(ObjectReference)`; `P ObjectReference.psc:737` `Bool Function HasOwner()` | R | Papyrus |
| Owner (native) | `C TESObjectREFRs.h:759` `[[nodiscard]] TESForm* GetOwner()` | R | C++ |
| Location keyword data (a **writable float per keyword per location**) | `P Location.psc:9` `Float Function GetKeywordData(Keyword akKeyword) Native`; `:43 Function SetKeywordData(Keyword akKeyword, Float afData) Native`; `:59 ModifyKeywordData(Keyword, Float)`; C++ `C TESForms.h:2542` `BSTArray<KEYWORD_DATA> keywordData; // 110`, struct `:2512 { BGSKeyword* keyword; float data; }` | R/W | both |
| Location hierarchy | `P Location.psc:21` `Bool Function IsChild(Location akOther) Native`; `:15 HasCommonParent(Location, Keyword)`; `:25 IsLinkedLocation(Location, Keyword)`; `:7 GetAllLinkedLocations(Keyword)`; `:5 AddLinkedLocation(Location, Keyword)`; C++ parent at `C TESForms.h:2525` `BGSLocation* parentLoc; // 050` | R/W | both |
| Location cleared | `P Location.psc:23,17,41` `IsCleared()`, `HasEverBeenCleared()`, `SetCleared(Bool)` | R/W | Papyrus |
| Location ref types | `P Location.psc:19` `HasRefType(LocationRefType)`, `:11 GetRefTypeAliveCount`, `:13 GetRefTypeDeadCount`; on a ref `P ObjectReference.psc:159 GetLocRefTypes()`, `:223 HasLocRefType()`, `:571 SetLocRefType(Location, LocationRefType)` | R/W | Papyrus |
| Location unreported-crime faction | `C TESForms.h:2526` `TESFaction* unreportedCrimeFaction; // 058` | R | **C++ only** |
| Unique NPCs registered to a location | `C TESForms.h:2534` `BSTArray<UniqueNPCData> uniqueNPCs; // 098` | R | **C++ only** |
| Encounter zone | `P ObjectReference.psc:139` `EncounterZone Function GetEncounterZone() Native`; `C TESObjectCELL::GetEncounterZone` `C TESForms.h:1520`; actors in one: `P EncounterZone.psc:7` `Actor[] Function GetActors(Keyword, Keyword) Native` | R | both |
| Workshop / home | `P companionactorscript.psc:180` `Location Property HomeLocation Auto mandatory`; `P ObjectReference.psc:205` `ObjectReference[] Function GetWorkshopOwnedObjects(Actor akActor) Native`; `:441 Event OnWorkshopNPCTransfer(Location akNewWorkshop, Keyword akActionKW)`; `:465 OpenWorkshopSettlementMenu(...)` | R | Papyrus |
| Distance / heading | `P ObjectReference.psc:619` `Float Function getDistance(ObjectReference akOther) Native`; `:143 GetHeadingAngle(ObjectReference)`; `:651 Bool Function IsNearPlayer()`; `:784 Bool Function IsInLocation(Location)` | R | Papyrus |
| Position | `P ObjectReference.psc:171,173,175` `GetPositionX/Y/Z()`; native `TESObjectREFR::GetPosition()` used at `R ActorScan.cpp:104` | R | both |
| Cell name | `C TESForms.h:1463-1465` `class TESObjectCELL : public TESForm, public TESFullName` | R | **C++ only** |
| Location name | `C TESForms.h:2502-2505` `class BGSLocation : public TESForm, TESFullName, BGSKeywordForm` | R | **C++ only** |
| Distance-threshold events | `P ScriptObject.psc:125,127` `RegisterForDistanceGreaterThanEvent(ScriptObject, ScriptObject, Float)`, `RegisterForDistanceLessThanEvent(...)`; fire at `:33,37` | R (event) | Papyrus |

**CORRECTION to the task's premise.** The brief said *"CommonLibF4 leaves `BGSLocation` and `TESObjectCELL` forward-declared."* **That is false in this checkout.** Both are fully defined: `BGSLocation` at `C TESForms.h:2502` (`sizeof == 0x140`, 18 members) and `TESObjectCELL` at `C TESForms.h:1463` (with `GetLocation()` at `:1527`, `GetEncounterZone()` at `:1520`, `GetDataX/Y()` at `:1506/:1513`, `GetCantWaitHere()` at `:1499`, `CELL_STATE` enum at `:1472`, `Flag` enum at `:1485`). The forward declarations the brief saw (`C BSExtraData.h:251`, `C TESForms.h:146`, `C FormComponents.h:113`, and six more for CELL) are the ordinary header-hygiene kind, not the whole story. **CHECKED:** `grep -rn "class __declspec(novtable) BGSLocation\|class BGSLocation;"` and the same for `TESObjectCELL`, across all 180 headers.

**NEGATIVE (verified):** `P Cell.psc` is 29 lines, read in full — **no name accessor**, no `GetLocation()`, no actor enumeration. Cell name is C++-only. Confirmed by `grep -rn "Function GetName|GetDisplayName"` over all 1808 base scripts: the only hits are AAF calling `GetDisplayName()` (`P AAF/AAF_MainQuestScript.psc:1344,1349`), which is not base Papyrus.

**NEGATIVE (verified):** `Location.HasKeyword` is **inherited from `P Form.psc:9** `Bool Function HasKeyword(Keyword akKeyword) Native`, so it needs a `Keyword` **form pointer** — a property filled in the CK or a `Game.GetFormFromFile` lookup. There is no string overload anywhere in P.

**NEGATIVE (verified):** **there is no `WorldSpace.psc`.** `find . -iname "worldspace*.psc"` over the whole tree returns nothing, yet `P ObjectReference.psc:211` declares `WorldSpace Function GetWorldSpace() Native`. The return type has no script, so the call is effectively undeclarable from Papyrus. `TESWorldSpace` **does** exist in C++ at `C TESWorldSpace.h:63`.

---

## 8. Appearance

| Item | Where | R/W | Layer |
|---|---|---|---|
| Worn-item keyword test | `P Actor.psc:546` `Bool Function WornHasKeyword(Keyword akKeyword) Native` | R | Papyrus |
| Is a given form equipped | `P Actor.psc:198` `Bool Function IsEquipped(Form akItem) Native` | R | Papyrus |
| Equipped weapon / shield / spell | `P Actor.psc:114,110,112` `GetEquippedWeapon(Int aiEquipIndex)`, `GetEquippedShield()`, `GetEquippedSpell(Int aiSource)` | R | Papyrus |
| Equipped slot type | `P Actor.psc:108` `Int Function GetEquippedItemType(Int aiEquipIndex) Native` | R | Papyrus |
| Equip / unequip | `P Actor.psc:70` `EquipItem(Form, Bool abPreventRemoval, Bool abSilent)`; `:538 UnequipItem(Form, Bool, Bool)`; `:540 UnequipItemSlot(Int aiSlot)`; `:536 UnequipAll()` | W | Papyrus |
| Outfit | `P Actor.psc:486` `Function SetOutfit(Outfit akOutfit, Bool abSleepOutfit) Native`; `P ActorBase.psc:33` same on the base; fields `C TESBoundAnimObjects.h:551-552` `BGSOutfit* defOutfit; // 2B0`, `BGSOutfit* sleepOutfit; // 2B8` | W (R in C++) | both |
| Head parts | `P Actor.psc:38` `Function ChangeHeadPart(headpart apHeadPart, Bool abRemovePart, Bool abRemoveExtraParts) Native`; C++ `C TESBoundAnimObjects.h:450` `[[nodiscard]] std::span<BGSHeadPart*> GetHeadParts(bool a_alternate = true) const`; fields `:555,558` `BGSHeadPart** headParts; // 2D0`, `std::int8_t numHeadParts; // 2E8`; alt list map `:437` | R/W | both |
| Eye texture | `P Actor.psc:466` `Function SetEyeTexture(textureset akNewTexture) Native` | W | Papyrus |
| Hair / facial-hair colour, face details | `C TESBoundAnimObjects.h:404-412` `struct HeadRelatedData { BGSColorForm* hairColor; BGSColorForm* facialHairColor; BGSTextureSet* faceDetails; }`; field `:534` `HeadRelatedData* headRelatedData; // 248` | R/W | **C++ only** |
| Body tint RGBA | `C TESBoundAnimObjects.h:560-563` `std::int8_t bodyTintColorR/G/B/A; // 2EA-2ED` | R/W | **C++ only** |
| Character tint layers | `C TESBoundAnimObjects.h:566` `BGSCharacterTint::Entries* tintingData; // 300` (header `C BGSCharacterTint.h`) | R/W | **C++ only** |
| Morph slider values | `C TESBoundAnimObjects.h:565` `BSTHashMap<std::uint32_t,float>* morphSliderValues; // 2F8`; region sliders `:556` `BSTArray<float>* morphRegionSliderValues; // 2D8`; facial-bone `:557` `BSTHashMap<std::uint32_t, BGSCharacterMorph::Transform>* facialBoneRegionSliderValues; // 2E0`; intensity `:523` `float GetFacialBoneMorphIntensity()` | R/W | **C++ only** |
| Body morph weight (thin/muscular/large) | `C TESBoundAnimObjects.h:540` `NiPoint3 morphWeight; // 278` | R/W | **C++ only** |
| Face template chain | `C TESBoundAnimObjects.h:539` `TESNPC* faceNPC; // 270`; `:478/:483` `GetRootFaceNPC()` | R | **C++ only** |
| Far skin | `C TESBoundAnimObjects.h:549` `TESObjectARMO* farSkin; // 2A0` | R | C++ |
| Alpha / refraction | `P Actor.psc:444` `SetAlpha(Float, Bool abFade)`; `C Actor.h:970-971` `SetAlpha(float)`, `GetAlpha()`; `C Actor.h:950` `SetRefraction(bool, float)` | W | both |
| Biped / worn armor changed hook | `C Actor.h:1356` `BSTSmartPointer<BipedAnim> biped; // 428`; `C Actor.h:1018` `virtual void WornArmorChanged(const BGSObjectInstance& a_object); // 12C` | R | **C++ only** |
| Inventory list | `C TESObjectREFRs.h:955` `BGSInventoryList* inventoryList; // 0F8`; `:738 FindAndWriteStackDataForItem(...)`; `:797 GetInventoryObjectCount(const TESBoundObject*)` | R | C++ |
| Equipped weight | `C Actor.h:1348` `float equippedWeight; // 3AC` |R| C++ |
| Facial expression, via AAF | `P AAF/AAF_API.psc:315` `Function ApplyMFGSet(Actor targetActor, String mfgSetID)`; `:322 AddMFGBlock(Actor, String morphIDList)`; `:329 RemoveMFGBlock(...)`; `:294 ApplyMorphSet(Actor, String morphSetID)`; `:301/:308 ApplyOverlaySet/RemoveOverlaySet(Actor, String)`; `:280 ApplyEquipmentSet(Actor, String)`; `:287 ApplyEquipmentRules(Actor, String tagList)` | W | Papyrus (AAF) |

**NEGATIVE (verified):** there is **no way to enumerate worn items from base Papyrus.** `P Actor.psc` (read in full) offers `WornHasKeyword`, `IsEquipped(Form)`, `GetEquippedWeapon/Shield/Spell`, `GetEquippedItemType` — all of which require you to already know what you are asking about. There is no `GetWornForm(slot)` and no worn-item array.

**NEGATIVE (verified):** `P Outfit.psc`, `P Armor.psc`, `P HeadPart.psc` are 1-line stubs — zero functions. You can hold one and pass it; you cannot inspect it.

**MISSED** — **nudity is not a readable state anywhere I opened.** There is no `IsNaked`, no body-slot mask accessor in Papyrus, and I did not find a biped-slot bitmask read on `Actor` in `C Actor.h`. Determining nudity would mean walking `inventoryList` in C++ and testing biped slots yourself — which I did not verify is possible from the headers I read.

**MISSED** — **LooksMenu / F4EE is not present in either checkout.** No `LooksMenu`/`F4EE` header in C, no such script in P. Every morph/tint field above is raw engine data, not the LooksMenu overlay API. `R Expressions.h:16-18` states Rapport deliberately uses *"an index into the engine's own facial morph table, so there is no texture to ship and no mod whose assets we are borrowing"* — and routes it through AAF's `ApplyMFGSet`, not through F4EE.

---

## 9. Persistence — what survives a save

### (a) F4SE co-save — proven working, and the only unlimited option

`R Ledger.cpp` is a complete reference implementation.

- `R Ledger.cpp:46` `bool Ledger::Register(const F4SE::SerializationInterface* a_intfc)`
- `R Ledger.cpp:55-58` `a_intfc->SetUniqueID(kPluginID); SetSaveCallback(OnSave); SetLoadCallback(OnLoad); SetRevertCallback(OnRevert);`
- FourCC record IDs: `R Ledger.cpp:20-26` `kPluginID = FourCC("RPRT")`, `kActorRecord = "ACTR"`, `kOverlayRecord = "OVRL"`, `kFaceRecord = "FACE"`, `kSceneRecord = "SCNE"`, `kPairRecord = "PAIR"`, `kVersion = 1`
- Write: `R Ledger.cpp:285` `a_intfc->OpenRecord(kActorRecord, kVersion)`, `:291/:303 WriteRecordData(...)`
- Read: `R Ledger.cpp:502` `void Ledger::Load(const F4SE::SerializationInterface*)`
- **Form-ID remapping across load order:** `R Ledger.cpp:243-244` `a_intfc->ResolveFormID(entry.first)` / `(entry.second)`, and `:571`, `:587` for partner ids. `R Ledger.cpp:234` records the scar: *"BOTH ids through ResolveFormID, like every other record here."*
- Per-actor record shape: `R Ledger.h:22-30` `struct ActorRecord { float lastSceneAt{-1.0f}; float lastRefusedAt{-1.0f}; std::uint32_t lastPartner{0}; std::uint32_t scenes{0}; std::uint32_t refusals{0}; float need{0.0f}; }`
- Save-format record: `R Ledger.h:87-96` `struct Entry { std::uint32_t formID; float lastSceneAt; float lastRefusedAt; std::uint32_t lastPartner; std::uint32_t scenes; std::uint32_t refusals; float need; }` — with the note at `:84-86` that every field being 4 bytes is what makes the load-time length check meaningful
- Ceilings: `R Ledger.cpp:30,34-35` `kMaxEntries = 100000`, `kMaxActorRecords = 2000`, `kMaxPairRecords = 4000` (*"2000 actor records is 56 KB and 4000 pairs is 64 KB"*)
- **Pair** history is a separate table because `lastPartner` only holds the most recent one — `R Ledger.h:68-77`, order-independent, `HoursSincePair`, `PairScenes`
- Time base: `R Ledger.cpp:63-70` `Ledger::GameHours()` from `RE::Calendar::GetSingleton()->gameDaysPassed`, returning **-1 for "no game loaded"** rather than 0 (`:67-69` records that `Calendar::GetHoursPassed` answers 24.0 there and that reads as a real hour)
- Failure is loud: `R Ledger.cpp:49-51` *"F4SE gave us no serialization interface - NOTHING WILL BE REMEMBERED between saves"*
- The design boundary is stated at `R Ledger.h:9-17`: the co-save holds **facts** (when, with whom, how many, refusals) and one opaque addon float `need`; "too soon"/"bored"/"wants company" are policy that lives elsewhere. `R Ledger.h:19-21` gives the reason it is in the save and not a file beside the plugin: *"a player with three characters has three sets of these facts."*

### (b) ActorValues as per-actor persistent storage

Vanilla does exactly this for companions — see §3. Written with `ObjectReference.SetValue/ModValue` (`P ObjectReference.psc:595,287`), read with `GetValue/GetBaseValue` (`:197,125`). Twelve-plus `CA_*` AVs at `P followersscript.psc:154-186` are nothing but durable per-actor floats. Persistence/clamp behaviour is governed by the AV form's own flags (`C ActorValueInfo.h:32-56`).

### (c) Ref-level keywords — runtime, per-reference

- `P ObjectReference.psc:31` `Function AddKeyword(Keyword apKeyword) Native`
- `P ObjectReference.psc:517` `Function RemoveKeyword(Keyword apKeyword) Native`
- `P ObjectReference.psc:527` `Function ResetKeyword(Keyword apKeyword) Native`
- Read: `P ObjectReference.psc:219` `HasKeyword(Keyword)`, `:221 HasKeywordInFormList(FormList)`
- Proven in use as a per-actor boolean: `P AAF/AAF_API.psc:399-413` `SetActorLocked` does `targetActor.AddKeyWord(AAF_ActorLocked)` / `RemoveKeyword(...)` with `HasKeyWord` as the read.
- Companion system uses the same trick: `P companionactorscript.psc:182` `Keyword[] Property KeywordsToAddWhileCurrentCompanion Auto`

### (d) Quest aliases

- `P ReferenceAlias.psc:9` `Function ForceRefTo(ObjectReference akNewRef) Native`, `:11 GetReference()`, `:7 Clear()`, `:339 ForceRefIfEmpty(ObjectReference)`, `:5 ApplyToRef(ObjectReference)`, `:337 RemoveFromRef(ObjectReference)`
- `P Quest.psc:18` `Alias Function GetAlias(Int aiAliasID) Native`
- An alias script carrying variables is per-actor storage that survives a save; `P ReferenceAlias.psc` also re-exposes the whole `Actor` event set (its `Event On…` block mirrors `Actor.psc`, e.g. `:137 OnItemRemoved`, `:145 OnKill`, `:153 OnLocationChange`, `:165 OnPackageChange`).

### (e) Globals and form lists

- `P GlobalVariable.psc:15,17,19,23,27` `GetValue()`, `SetValue(Float)`, `GetValueInt()`, `SetValueInt(Int)`, `Mod(Float)` — one value, game-wide, not per-actor
- `P FormList.psc:5,7,9,11,13,15,17` `AddForm(Form)`, `Find(Form)`, `GetAt(Int)`, `GetSize()`, `HasForm(Form)`, `RemoveAddedForm(Form)`, `Revert()` — runtime additions persist; this is a workable per-actor *set* membership store

### (f) Script variables on a quest

`P followersscript.psc:295-299` shows `Auto conditional hidden` properties (`AllFollowerState`, `isPlayerLoitering`, `AutonomyAllowed`) used as persistent script state readable by dialogue conditions.

**NEGATIVE (verified):** **PapyrusUtil / `StorageUtil` is not present.** `ls` of P found no `StorageUtil.psc`, `PapyrusUtil.psc`, `JContainers`, or `JValue`; `grep -rl "StorageUtil"` over all 1808 scripts returned **zero files**. There is no vanilla string-keyed per-form store in FO4. `P CanarySaveFileMonitor.psc` is listed under `P AAF\` (save-corruption watchdog, not storage) — I did **not** open it.

**NEGATIVE:** `P Utility.psc` (36 lines, read in full) has no persistence of any kind — `Wait`, `RandomInt/Float`, `GetCurrentGameTime`, `GameTimeToString`, INI setters, budget/framerate probes, `CallGlobalFunction`. Notably it **does** offer `Float Function GetCurrentGameTime() Global Native` (`:34`) and `Function WaitGameTime(Float afHours) Global Native` (`:8`).

---

## 10. Events a flirt system can hook

### On `Actor` (`P Actor.psc`) — also all available remotely via `RegisterForRemoteEvent`

`:256 OnCombatStateChanged(Actor akTarget, Int aeCombatState)` · `:260 OnCommandModeCompleteCommand(Int, ObjectReference)` · `:264/:268 OnCommandModeEnter/Exit()` · `:272 OnCommandModeGiveCommand(Int, ObjectReference)` · `:276 OnCompanionDismiss()` · `:280 OnConsciousnessStateChanged(Bool abUnconscious)` · `:284 OnCripple(ActorValue, Bool)` · `:288 OnDeath(Actor akKiller)` · `:292 OnDeferredKill(Actor)` · `:300 OnDying(Actor)` · `:304 OnEnterBleedout()` · **`:308 OnEnterSneaking()`** · `:312/:316 OnEscortWaitStart/Stop()` · **`:320 OnGetUp(ObjectReference akFurniture)`** · **`:324 OnItemEquipped(Form akBaseObject, ObjectReference akReference)`** · `:328 OnItemUnequipped(...)` · `:332 OnKill(Actor akVictim)` · **`:336 OnLocationChange(Location akOldLoc, Location akNewLoc)`** · **`:340 OnPackageChange(Package akOldPackage)`** · **`:344 OnPackageEnd(Package)`** · **`:348 OnPackageStart(Package akNewPackage)`** · `:352 OnPartialCripple(ActorValue, Bool)` · `:356 OnPickpocketFailed()` · `:360 OnPlayerCreateRobot(Actor)` · `:364 OnPlayerEnterVertibird(ObjectReference)` · `:368 OnPlayerFallLongDistance(Float)` · `:372 OnPlayerFireWeapon(Form)` · `:376 OnPlayerHealTeammate(Actor akTeammate)` · `:380 OnPlayerLoadGame()` · `:384 OnPlayerModArmorWeapon(Form, objectmod)` · `:388 OnPlayerModRobot(Actor, objectmod)` · `:392 OnPlayerSwimming()` · `:396 OnPlayerUseWorkBench(ObjectReference)` · `:400 OnRaceSwitchComplete()` · **`:404 OnSit(ObjectReference akFurniture)`** · **`:408 OnSpeechChallengeAvailable(ObjectReference akSpeaker)`**

### On `ObjectReference` (`P ObjectReference.psc`) — an Actor is one

**`:297 OnActivate(ObjectReference akActionRef)`** · `:301/:305/:309 OnCellAttach/Detach/Load()` · `:317 OnContainerChanged(ObjectReference akNewContainer, ObjectReference akOldContainer)` · `:325 OnEquipped(Actor akActor)` · `:329 OnExitFurniture(ObjectReference)` · **`:345 OnItemAdded(Form akBaseItem, Int aiItemCount, ObjectReference akItemReference, ObjectReference akSourceContainer)`** · **`:349 OnItemRemoved(...)`** · `:353 OnLoad()` / `:433 OnUnload()` · **`:369 OnPlayerDialogueTarget()`** · `:385 OnRelease()` / `:333 OnGrab()` · `:389 OnReset()` · **`:393 OnSell(Actor akSeller)`** · `:397 OnSpellCast(Form)` · `:421/:425 OnTriggerEnter/Leave(ObjectReference akActionRef)` · `:429 OnUnequipped(Actor akActor)` · `:437 OnWorkshopMode(Bool)` · `:441 OnWorkshopNPCTransfer(Location akNewWorkshop, Keyword akActionKW)`

### On `ScriptObject` (`P ScriptObject.psc`) — available to any script, with registration

**`:49 OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Projectile akProjectile, Bool abPowerAttack, Bool abSneakAttack, Bool abBashAttack, Bool abHitBlocked, String asMaterialName)`** — register at `:129` `RegisterForHitEvent(ScriptObject akTarget, ScriptObject akAggressorFilter, Form akSourceFilter, Form akProjectileFilter, Int aiPowerFilter, Int aiSneakFilter, Int aiBashFilter, Int aiBlockFilter, Bool abMatch)`

- **`:45 OnGainLOS(ObjectReference akViewer, ObjectReference akTarget)`** / **`:61 OnLostLOS(...)`** — register `:117 RegisterForDetectionLOSGain(Actor akViewer, ObjectReference akTarget)`, `:119 …Lost`, `:121 RegisterForDirectLOSGain(ObjectReference akViewer, ObjectReference akTarget, String asViewerNode, String asTargetNode)`, `:123 …Lost`
- **`:33 OnDistanceGreaterThan` / `:37 OnDistanceLessThan(ObjectReference akObj1, ObjectReference akObj2, Float afDistance)`** — register `:125/:127`
- `:21 OnAnimationEvent(ObjectReference akSource, String asEventName)` — register `:113`; `:25 OnAnimationEventUnregistered`
- `:65 OnMagicEffectApply(ObjectReference akTarget, ObjectReference akCaster, MagicEffect akEffect)` — register `:133`
- `:69 OnMenuOpenCloseEvent(String asMenuName, Bool abOpening)` — register `:135`
- **`:73 OnPlayerSleepStart(Float afSleepStartTime, Float afDesiredSleepEndTime, ObjectReference akBed)`** / `:77 OnPlayerSleepStop(Bool abInterrupted, ObjectReference akBed)` — register `:137`
- `:81 OnPlayerTeleport()` — register `:139`; `:85/:89 OnPlayerWaitStart/Stop` — register `:141`
- `:93 OnRadiationDamage(ObjectReference akTarget, Bool abIngested)` — register `:143`
- `:97 OnTimer(Int aiTimerID)` / `:101 OnTimerGameTime(Int)` — `:161 StartTimer(Float afInterval, Int aiTimerID)`, `:163 StartTimerGameTime(...)`, `:11/:13 CancelTimer/CancelTimerGameTime`
- `:105 OnTrackedStatsEvent(String arStatName, Int aiStatValue)` — register `:147 RegisterForTrackedStatsEvent(String asStat, Int aiThreshold)`
- `:57 OnLooksMenuEvent(Int aiFlavor)` — register `:131`
- Custom events: `:115 RegisterForCustomEvent(ScriptObject akSender, String asEventName)`, `:155 SendCustomEvent(String asEvent, Var[] akArgs)`; remote: `:145 RegisterForRemoteEvent(ScriptObject akEventSource, String asEventName)`
- Inventory filtering: `:5 AddInventoryEventFilter(Form akFilter)`, `:153/:151 Remove…`

### Story-manager events (`P Quest.psc`) — the richest social channel

**`:112 OnStoryDialogue(Location akLocation, ObjectReference akActor1, ObjectReference akActor2)`** · **`:132 OnStoryHello(Location akLocation, ObjectReference akActor1, ObjectReference akActor2)`** · **`:124 OnStoryFlatterNPC(ObjectReference akActor)`** · **`:144 OnStoryIntimidateNPC(ObjectReference akActor)`** · **`:84 OnStoryBribeNPC(ObjectReference akActor)`** · **`:184 OnStoryPlayerGetsFavor(ObjectReference akActor)`** · **`:188 OnStoryRelationshipChange(ObjectReference akActor1, ObjectReference akActor2, Int aiOldRelationship, Int aiNewRelationship)`** · `:60 OnStoryActivateActor(Location, ObjectReference)` · `:64 OnStoryActorAttach(ObjectReference, Location)` · `:76 OnStoryAssaultActor(ObjectReference akVictim, ObjectReference akAttacker, Location, Int aiCrime)` · `:80 OnStoryAttractionObject(ObjectReference akActor, ObjectReference akObject, Location, Bool abCommanded)` · `:92 OnStoryChangeLocation(ObjectReference akActor, Location akOld, Location akNew)` · `:116 OnStoryDiscoverDeadBody(ObjectReference akActor, ObjectReference akDeadActor, Location)` · `:156 OnStoryKillActor(ObjectReference akVictim, ObjectReference akKiller, Location, Int aiCrimeStatus, Int aiRelationshipRank)` · `:180 OnStoryPickPocket(ObjectReference akVictim, Bool abSuccess)` · `:204 OnStoryTrespass(...)` · `:196 OnStoryScript(Keyword akKeyword, Location akLocation, ObjectReference akRef1, ObjectReference akRef2, Int aiValue1, Int aiValue2)`

**Firing your own story event:** `P Keyword.psc:5` `Function SendStoryEvent(Location akLoc, ObjectReference akRef1, ObjectReference akRef2, Int aiValue1, Int aiValue2) Native`; `:7 Bool Function SendStoryEventAndWait(...)`.

### Package / scene / alias

`P Package.psc:9,13,17` `Event OnChange(Actor akActor)`, `OnEnd(Actor)`, `OnStart(Actor)` · `P Scene.psc:13,17,21,25,29` `OnAction(Int auiActionID, ReferenceAlias akAlias)`, `OnBegin()`, `OnEnd()`, `OnPhaseBegin(Int)`, `OnPhaseEnd(Int)` · `P Alias.psc:7,11,15` `OnAliasInit/Reset/Shutdown()` · `P Location.psc:29,33` `OnLocationCleared()`, `OnLocationLoaded()` · `P Quest.psc:44,48,52,56` `OnQuestInit()`, `OnQuestShutdown()`, `OnReset()`, `OnStageSet(Int auiStageID, Int auiItemID)`

### C++ event sinks (`C Events.h`)

`:543 TESActivateEvent` · `:559 TESContainerChangedEvent` · `:579 TESDeathEvent` · **`:596 TESEnterSneakingEvent`** · `:610 TESEquipEvent` · **`:644 TESFurnitureEvent`** · `:741 TESHitEvent` (with `:681 class HitData`, `:670 DamageImpactData`) · `:762 TESMagicEffectApplyEvent` · `:779 TESObjectLoadedEvent` · `:795 TESSwitchRaceCompleteEvent` · `:629 TESFormDeleteEvent` · `:55 BGSActorEvent` → `:63 BGSActorCellEvent`, `:78 BGSActorDeathEvent` · `:159 CellAttachDetachEvent` · `:361 MenuOpenCloseEvent` · `:343 MenuModeChangeEvent` · `:244 DoBeforeNewOrLoadCompletedEvent` · `:513 NameChangedEvent` · `:845 PositionPlayerEvent`. The `Actor` class is itself an event **source** for `MovementMessageUpdateRequestImmediate`, `PerkValueEvents::PerkValueChangedEvent`, `PerkValueEvents::PerkEntryUpdatedEvent`, `ActorCPMEvent` (`C Actor.h:858-861`).

**NEGATIVE (verified):** there is **no `OnDialogueBegin`/`OnDialogueEnd`, no `OnTopicSaid`, no `OnGreet`** in base Papyrus. The closest are `OnPlayerDialogueTarget()` (`P ObjectReference.psc:369`, player-only, fires when they become your dialogue target), `IsTalking()` polling, and the story events `OnStoryDialogue`/`OnStoryHello`. There is no `Topic.psc` and no `TopicInfo.psc` (`ls` confirms; the `*TopicInfo*.psc` files present are quest fragments, not the base type) — so **a `Topic` cannot be inspected from Papyrus at all**, only passed to `Say`.

**NEGATIVE:** there is **no `VoiceType.psc`**, though `P ObjectReference.psc:201 GetVoiceType()` returns one and `P Actor.psc:488 SetOverrideVoiceType(VoiceType)` takes one. Same shape as the `WorldSpace` problem.

---

## 11. The AAF relationship layer — present and empty

`P AAF/AAF_API.psc` exposes a stat system that looks like exactly the right substrate and is not populated:

- `:366 Function ChangeStat(Actor targetActor, String statID, Float statValue)`
- **`:374 Function ChangeRelationshipStat(Actor targetActor, Actor relationActor, String statID, Float statValue)`**
- `:383/:391 ChangeStatMinimum/Maximum(Actor, String statID, Float)`
- `:360 Function GetActorData(Actor targetActor)` → fires `"GET_ACTORDATA"` via `AAF_MainQuestScript.makeActorData(targetActor, False)`
- **`:488 Float Function GetAttraction(Actor attractor, Actor attractee)`** → `AAF_MainQuestScript.GetAttraction(attractor, attractee)` at `:489`
- Roles: `:464 AssignRole(Actor, String role)`, `:471 RemoveRole(Actor, String)`, `:478 ClearAllRoles(Actor)`
- Locking: `:399 Bool Function SetActorLocked(Actor targetActor, Bool value)` (implemented as a keyword, `:404`/`:408`)

**Corroborated by a prior finding in this project's memory** (`aaf-stat-layer-is-empty`): `GetAttraction` exists but **no relationship stat is defined anywhere**, and only AAF itself calls the stat API. **CHECKED:** the API declarations above, verbatim. **MISSED:** I did not re-verify the "no stat definitions exist" half in this pass — I did not open `AAF_MainQuestScript.psc` beyond a `GetDisplayName` grep, and I did not search the AAF XML data for `<stat>` definitions.

---

## 12. Consolidated NEGATIVES — the absences that shape a design

Each of these I verified by opening the file or by an exhaustive `ls`/`find`/`grep`:

1. **No name accessor in base Papyrus, anywhere.** `Form.psc` (17 lines, whole file read) has no `GetName`. `ObjectReference.psc` (791 lines, all declarations read) has no `GetDisplayName`. `grep -rn "Function GetName|GetDisplayName|SetDisplayName"` over all 1808 base scripts hits only AAF calling it. `TESObjectREFR::GetDisplayFullName()` (`C TESObjectREFRs.h:750`) is C++-only and is why `Rapport:Core.ActorName` exists.
2. **No `WorldSpace.psc`** — yet `ObjectReference.GetWorldSpace()` is declared (`P ObjectReference.psc:211`). `TESWorldSpace` exists in C++ (`C TESWorldSpace.h:63`).
3. **No `Topic.psc`, no `VoiceType.psc`** — both are returned or taken by declared functions.
4. **No `Relationship.psc`.** `BGSRelationship` is C++-only (`C TESForms.h:2881`), and its `packedData` (`:2893`) has no accessor in the header.
5. **Eleven types are 1-line stubs with zero functions:** `ActorValue`, `Race`, `Class`, `AssociationType`, `Outfit`, `Armor`, `HeadPart`, `CombatStyle`, `Idle`, `LocationRefType`, `Action`. You can hold them, compare them, pass them — nothing else.
6. **`Cell` has no name accessor** (`P Cell.psc`, 29 lines, read whole) — but `TESObjectCELL` inherits `TESFullName` in C++ (`C TESForms.h:1465`).
7. **`Location.HasKeyword` is `Form.HasKeyword`** (`P Form.psc:9`) and needs a hardcoded `Keyword` **form** — a CK property or `Game.GetFormFromFile`. No string overload exists.
8. **No `StorageUtil`/PapyrusUtil/JContainers** in this install — `grep -rl "StorageUtil"` over 1808 scripts: **zero**. No vanilla string-keyed per-form store.
9. **`ExtraFactionChanges` has no class in CommonLibF4** — only the `kFactionChanges` enum entry (`C BSExtraData.h:123`). Runtime faction changes are invisible to the cheap C++ `npc->factions` read, which is why `R Pairing.cpp:21-23` calls its own faction test *"a weighting signal, not a gate."*
10. **`talkingToPlayer` does not mean "talking."** It is player-specific (`C Actor.h:788`). Two NPCs mid-conversation read 0 — documented from a live repro at `R ActorScan.cpp:112-129`. Use `BOOL_FLAGS::kInRandomScene` (`C Actor.h:875`).
11. **No worn-item enumeration from Papyrus.** Only keyword/form tests. No nudity state anywhere I opened.
12. **No LooksMenu/F4EE surface** in either checkout. Morph/tint access is raw `TESNPC` fields, C++-only.
13. **No age concept.** `IsChild()` is the only age-adjacent read that exists.
14. **No per-NPC affinity between two NPCs.** The whole `CA_*` system is companion→player.
15. **Decompiled sources carry no default arguments** — stated at `X:19-20`: *"the base sources are decompiled and carry no default argument values, so every argument is passed explicitly."* Every call above must pass every parameter.
16. **The brief's claim about `BGSLocation`/`TESObjectCELL` being forward-declared is wrong** in this checkout — see §7 for the full correction with line numbers.

---

## Cheap vs expensive

The distinction that already decided several of Rapport's designs, with the citations that establish each cost.

### Free — a field read, no call at all

`actor->race` (`R ActorScan.cpp:109`) · `actor->boolFlags` (`C Actor.h:1359`) · `talkingToPlayer` and every `ActorState` bitfield (`C Actor.h:783-804`) · `parentCell` via `GetParentCell()` which is `noexcept` and returns the member directly (`C TESObjectREFRs.h:766`) · `npc->factions` (`C FormComponents.h:1617`) · `currentCombatTarget`, `myKiller`, `editorLocation`, `vendorFaction`, `speakingAnimArchType` (`C Actor.h:1337,1338,1325,1332,1328`) · `TESNPC` appearance fields (`C TESBoundAnimObjects.h:530-566`). These are the reads you can do on every actor on every pass without a budget.

### One native call — cheap, but count them

`GetFormID()`, `GetPosition()`, `Get3D()`, `IsChild()`, `IsDead(bool)`, `IsInCombat()`, `GetHostileToActor()`, `GetDisplayFullName()`, `GetLevel()`, `GetHealthPercent()`, `GetNPC()`. Rapport does all of these per actor and still budgets the pass: `R ActorScan.h:29-32` `Begin(float a_radius)` / `[[nodiscard]] bool Step(float a_budgetMs)`, sliced across frames, with the clock read only every 16 actors — `R ActorScan.cpp:30-32`: *"Reading the clock per actor would cost more than the filters it is meant to bound."*

Note the ordering discipline in `R ActorScan.cpp:90-143`: `stale → notLoaded → IsChild → IsDead` come **before** `IsInCombat → race → talkingToPlayer → kInRandomScene → IsQuestDriven → distance`. Cheapest-and-most-eliminating first, and the distance test is *last* despite being arithmetic, because by then the population is small.

### Per-actor-per-pass, and the reason `ScanCounters` exists

`R ActorScan.h:7-21` `struct ScanCounters` records *why* each actor was dropped — `stale`, `notLoaded`, `child`, `dead`, `inCombat`, `outOfRange`, `raceNotAllowed`, `inDialogue`, `inRandomScene`, `questDriven`. `R ActorScan.h:36-38` gives the reason: *"A count alone cannot tell 'this cell is full of dogs' from 'the race field is wrong', and that difference decides whether there is a bug."* If a read is expensive enough to filter on, it is expensive enough to count.

### Quadratic — the real cliff

`R Pairing.cpp:91-92` is an O(n²) double loop over candidates, and inside it `R Pairing.cpp:126-133` loops **every observer position** for every pair. That is O(pairs × observers). The mitigations visible in the code:

- Observer positions are **collected once during the scan** (`R ActorScan.cpp:104` `_observerPositions.push_back(actor->GetPosition())`) rather than re-fetched per pair.
- The hostility gate `first->GetHostileToActor(second)` (`R Pairing.cpp:107`) is placed **after** the cheap distance reject at `:102`, so the expensive native runs only on pairs already close enough.
- `ShareAFaction` (`R Pairing.cpp:13-35`) is itself O(factions²) per pair — and is deliberately limited to the **base** faction array precisely because that is a flat memory walk, not a call: `R Pairing.cpp:21-23`.

### The one the owner's own code refused to pay

`X:596-598` — on cell ownership: *"Only asked for INTERIOR pairs. An exterior cell being unowned says nothing, and asking would cost two native calls for every actor in every pair on every pass — up to forty-eight of them — to learn nothing."*

`WhosePlace` (`X:599-625`) costs `GetParentCell()` + `GetActorOwner()` + `GetActorBase()` ×2 + possibly `GetFactionOwner()` + `IsInFaction()` ×2 — up to **eight Papyrus native calls per pair** — and is gated behind a cached `CandidateInterior(index)` flag published by the plugin. The result is then snapshotted, never recomputed: `X:627` *"From the snapshot, never recomputed."*

### Cross-boundary is the most expensive thing here

Every Papyrus→native call crosses the VM boundary. `X:522-528` documents paying it deliberately for names (unreadable logs being a correctness failure), and `X:265-268` in `Bridge.psc` batches both actors' sexes into two calls *"while we hold real Actors"* rather than resolving form ids later. The general shape: **resolve in C++, publish a flat snapshot, let Papyrus read indices.** Rapport exposes ~60 `BindNativeMethod` entries (`R PapyrusLink.cpp:733-793`) that are almost all zero-argument index reads for exactly this reason.

### Save-side

Co-save writes are bounded by hard ceilings, not by how many actors you have seen: `R Ledger.cpp:34-35` `kMaxActorRecords = 2000`, `kMaxPairRecords = 4000` — *"2000 actor records is 56 KB and 4000 pairs is 64 KB."* Reading from the ledger is a hash lookup and is free at any poll rate.

---

## Where I stopped

**MISSED** — the following would each need a fresh pass and I did not do them:

- `SIT_SLEEP_STATE`, `DIALOGUE_TYPE`, `DIALOGUE_SUBTYPE`, `COMMAND_TYPE`, `ACTOR_LIFE_STATE`, `FIGHT_REACTION` enum bodies — all named in headers I read, none of their definitions opened (most sit above `C Actor.h:740`).
- The integer scales behind `GetRelationshipRank`, `GetCombatState`, `GetSitState`, `GetSleepState`, `GetFactionReaction`, `GetEquippedItemType`, `WouldRefuseCommand` — declared `Native` with no enum in any file I opened.
- `MenuTopicManager.h`, `BGSStoryEventManager.h`, `TESPackages.h`, `TESRace.h`, `BGSCharacterMorph.h`, `BGSCharacterTint.h`, `ProcessLists.h`, `Calendar.h` — all present in C, none opened.
- `AAF_MainQuestScript.psc` (only grepped) and the AAF XML data — so the "AAF has no relationship stats defined" half of §11 is carried from prior project memory, not re-verified in this pass.
- Whether a runtime `SetRelationshipRank` is reachable from C++ at all. I found no extra-data class for it.
- Which AVs actually persist in the save. The flags exist (`kCachePermenant`, `kDoesNotRecover`); I found no code reading them.
- `P CanarySaveFileMonitor.psc` — listed under `P AAF\`, not opened.

**Tooling note:** none of these three trees is in the GitNexus index, so graph tools return zero for all of them and that zero means nothing. Everything above is a direct file read, taken during a `bearing:fallback` window opened for exactly this reason.
