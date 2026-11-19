// Living Intelligence System (LIS): data types shared by every LIS component.
// Plain C++ data so the core runs headless, deterministically and in tests.
#pragma once

#include "CoreMinimal.h"

AETHRALIS_API DECLARE_LOG_CATEGORY_EXTERN(LogAethraLIS, Log, All);

// New enum values go at the end (before Count): saves store them as numbers.
enum class ELisTrait : uint8 { Courage, Ambition, Empathy, Curiosity, Aggression, Loyalty, Sociability, Caution, Count };
enum class ELisNeed : uint8 { Safety, Food, Rest, Work, Social, Purpose, Count };
enum class ELisPlace : uint8 { Village, Fields, Forest, River, Market, Wilds, Count };
enum class ELisProfession : uint8 { Farmer, Fisher, Woodcutter, Merchant, Guard, Explorer, Count };
enum class ELisAction : uint8 { Idle, Eat, Sleep, Farm, Fish, Chop, Sell, BuyFood, AskHelp, GiveHelp, Socialize, Explore, Patrol, Travel, Count };
enum class ELisGoal : uint8 { Survive, MasterCraft, Prosper, SupportCommunity, Discover, Count };
enum class ELisEvent : uint8
{
    FoodShortage, ResourceFound, ResourceDepleted, ResourceRemoved, BlockPlaced,
    HelpGiven, HelpRefused, TradeSucceeded, TradeFailed, Conflict, Danger, DangerRepelled, GoalChanged, Count
};
enum class ELisSource : uint8 { Observed, Told };
enum class ELisTier : uint8 { Near, Mid, Far };

template <typename E> constexpr int32 LisNum() { return static_cast<int32>(E::Count); }
template <typename E> constexpr int32 LisIdx(E Value) { return static_cast<int32>(Value); }

AETHRALIS_API const TCHAR* LisName(ELisTrait Value);
AETHRALIS_API const TCHAR* LisName(ELisNeed Value);
AETHRALIS_API const TCHAR* LisName(ELisPlace Value);
AETHRALIS_API const TCHAR* LisName(ELisProfession Value);
AETHRALIS_API const TCHAR* LisName(ELisAction Value);
AETHRALIS_API const TCHAR* LisName(ELisGoal Value);
AETHRALIS_API const TCHAR* LisName(ELisEvent Value);

// Actor ids: NPCs use 1..N. 0 means nobody/unknown, negative ids are players.
constexpr int32 LisNobody = 0;
constexpr int32 LisPlayer = -1;

struct FLisIdentity
{
    int32 Id = LisNobody;
    FString Name;
    int32 Age = 30;
    ELisProfession Profession = ELisProfession::Farmer;
    FString Origin;
};

// Each trait in [0, 1]; 0.5 is average.
struct FLisPersonality
{
    float Value[LisNum<ELisTrait>()] = {};

    float operator[](ELisTrait Trait) const { return Value[LisIdx(Trait)]; }
    float& operator[](ELisTrait Trait) { return Value[LisIdx(Trait)]; }
};

// Directed relation from the owner to another actor. A->B is stored apart from B->A.
// Trust, respect and affection start neutral at 0.5; fear and resentment start at 0.
struct FLisRelation
{
    float Trust = 0.5f;
    float Respect = 0.5f;
    float Affection = 0.5f;
    float Fear = 0.f;
    float Resentment = 0.f;
};

// The objective fact, kept in the simulation log. NPCs never read it directly.
struct FLisEventRecord
{
    int32 Id = 0;
    ELisEvent Type = ELisEvent::FoodShortage;
    ELisPlace Place = ELisPlace::Village;
    int64 Hour = 0;
    int32 Actor = LisNobody;
    int32 Target = LisNobody;
    float Amount = 0.f;
    FIntVector Cell = FIntVector::ZeroValue;
};

// One NPC's account of an event: observed or heard, with its own interpretation.
struct FLisMemory
{
    int32 EventId = 0;
    ELisEvent Type = ELisEvent::FoodShortage;
    ELisPlace Place = ELisPlace::Village;
    int64 Hour = 0;          // when it happened
    int64 LearnedHour = 0;   // when this NPC learned it
    int32 Actor = LisNobody;
    int32 Target = LisNobody;
    float Amount = 0.f;
    ELisSource Source = ELisSource::Observed;
    int32 ToldBy = LisNobody;
    float Confidence = 1.f;  // 0..1
    float Relevance = 0.f;   // 0..2, for this NPC
    float Valence = 0.f;     // -1..1, how this NPC judged it
};

// What an NPC believes about a place. Updated only by perception or conversation.
struct FLisPlaceBelief
{
    bool bKnown = true;
    float Stock = 0.f;
    float Risk = 0.f;        // learned danger estimate, 0..1
    float Confidence = 0.f;
    int64 Hour = 0;
    ELisSource Source = ELisSource::Observed;
};

struct FLisNpc
{
    FLisIdentity Identity;
    FLisPersonality Personality;
    float Needs[LisNum<ELisNeed>()] = {};            // deficit: 0 satisfied, 1 critical
    ELisGoal Goal = ELisGoal::Survive;
    ELisPlace Place = ELisPlace::Village;
    float Food = 2.f;
    float Wood = 0.f;
    float Coins = 5.f;
    float Skill[LisNum<ELisProfession>()] = {};       // 0..1, grows with practice
    float Expectation[LisNum<ELisAction>()] = {};     // learned outcome bias, -1..1
    FLisPlaceBelief Beliefs[LisNum<ELisPlace>()];     // Village stock = community granary
    float BelievedFoodPrice = 2.f;
    TArray<FLisMemory> Memories;
    TMap<int32, FLisRelation> Relations;

    ELisAction Current = ELisAction::Idle;   // running step (Travel or the intent)
    ELisAction Intent = ELisAction::Idle;    // the chosen action the plan leads to
    ELisPlace IntentPlace = ELisPlace::Village;
    int32 IntentTarget = LisNobody;
    int64 BusyUntil = 0;
    int64 NextThink = 0;
    ELisTier Tier = ELisTier::Near;

    int32 Decisions = 0;
    int32 Replans = 0;
    int32 GoalChanges = 0;
    int32 ActionCount[LisNum<ELisAction>()] = {};

    float Need(ELisNeed Which) const { return Needs[LisIdx(Which)]; }
    float& Need(ELisNeed Which) { return Needs[LisIdx(Which)]; }
    float Trait(ELisTrait Which) const { return Personality[Which]; }
    const FLisRelation* FindRelation(int32 Other) const { return Relations.Find(Other); }
    FLisRelation& Relation(int32 Other) { return Relations.FindOrAdd(Other); }
    const FLisMemory* FindMemory(int32 EventId) const { return Memories.FindByPredicate([EventId](const FLisMemory& M) { return M.EventId == EventId; }); }
};

struct FLisPlaceState
{
    float Stock = 0.f;
    float Capacity = 0.f;
    float Regen = 0.f;       // per hour
    float Danger = 0.f;      // chance of a threat per working hour
    float BaseDanger = 0.f;
    FVector2f Pos = FVector2f::ZeroVector;   // abstract map, 1 unit ~ 1 hour of walking
    FIntVector Cell = FIntVector::ZeroValue; // voxel cell the place is anchored to
};

struct FLisWorld
{
    FLisPlaceState Places[LisNum<ELisPlace>()];
    float Granary = 40.f;     // community food store in the village
    float FoodPrice = 2.f;
    float WoodPrice = 1.5f;
    float TradeRisk = 0.12f;  // base chance a negotiation goes wrong

    FLisPlaceState& Place(ELisPlace Which) { return Places[LisIdx(Which)]; }
    const FLisPlaceState& Place(ELisPlace Which) const { return Places[LisIdx(Which)]; }
};

AETHRALIS_API ELisPlace LisWorkplace(ELisProfession Profession);
AETHRALIS_API ELisProfession LisSkillFor(ELisAction Action);
