#include "LisSimulation.h"

#include "HAL/PlatformTime.h"

namespace
{
uint64 Mix(uint64 X)
{
    X += 0x9E3779B97F4A7C15ull;
    X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
    X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
    return X ^ (X >> 31);
}

float Hash01(uint32 Seed, int64 Hour, int32 Actor, uint32 Salt)
{
    const uint64 H = Mix(Seed ^ Mix(static_cast<uint64>(Hour) ^ Mix((static_cast<uint64>(static_cast<uint32>(Actor)) << 32) | Salt)));
    return static_cast<float>(H >> 40) / static_cast<float>(1 << 24);
}

constexpr int64 GenerationHour = -1; // generation draws are independent of simulated time

constexpr int32 NumActions = LisNum<ELisAction>();
constexpr int32 NumNeeds = LisNum<ELisNeed>();

// How much each action satisfies each need: Safety, Food, Rest, Work, Social, Purpose.
constexpr float Satisfies[NumActions][NumNeeds] = {
    { 0.f, 0.f, .15f, 0.f, 0.f, 0.f },   // Idle
    { 0.f, 2.2f, 0.f, 0.f, 0.f, 0.f },   // Eat: basic needs dominate once critical
    { 0.f, 0.f, 2.f, 0.f, 0.f, 0.f },    // Sleep
    { 0.f, .3f, 0.f, 1.f, 0.f, .3f },    // Farm
    { 0.f, .3f, 0.f, 1.f, 0.f, .3f },    // Fish
    { 0.f, 0.f, 0.f, 1.f, 0.f, .3f },    // Chop
    { 0.f, .15f, 0.f, .4f, .1f, .2f },   // Sell
    { 0.f, .9f, 0.f, 0.f, 0.f, 0.f },    // BuyFood
    { 0.f, .8f, 0.f, 0.f, .2f, 0.f },    // AskHelp
    { 0.f, 0.f, 0.f, 0.f, .3f, .8f },    // GiveHelp
    { .1f, 0.f, 0.f, 0.f, 1.1f, .1f },   // Socialize
    { 0.f, .15f, 0.f, .5f, 0.f, .8f },   // Explore
    { .6f, 0.f, 0.f, .8f, 0.f, .4f },    // Patrol
    { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f },    // Travel (never scored)
};

constexpr int32 Durations[NumActions] = { 1, 1, 7, 3, 3, 3, 1, 1, 1, 1, 2, 4, 3, 0 };

constexpr float BaseRelevance[LisNum<ELisEvent>()] = { .8f, .7f, .4f, .5f, .2f, .5f, .6f, .2f, .5f, .7f, .8f, .5f, .3f };

bool IsOutdoor(ELisPlace Place)
{
    return Place == ELisPlace::Fields || Place == ELisPlace::Forest || Place == ELisPlace::River || Place == ELisPlace::Wilds;
}

bool IsWork(ELisAction Action)
{
    return LisSkillFor(Action) != ELisProfession::Count;
}

void ClampRelation(FLisRelation& R)
{
    R.Trust = FMath::Clamp(R.Trust, 0.f, 1.f);
    R.Respect = FMath::Clamp(R.Respect, 0.f, 1.f);
    R.Affection = FMath::Clamp(R.Affection, 0.f, 1.f);
    R.Fear = FMath::Clamp(R.Fear, 0.f, 1.f);
    R.Resentment = FMath::Clamp(R.Resentment, 0.f, 1.f);
}

void ClampNeeds(FLisNpc& Npc)
{
    for (float& Need : Npc.Needs)
    {
        Need = FMath::Clamp(Need, 0.f, 1.f);
    }
}

float GoalBonus(ELisGoal Goal, ELisAction Action, ELisProfession Profession)
{
    switch (Goal)
    {
    case ELisGoal::Survive:
        return Action == ELisAction::BuyFood ? .15f : (Action == ELisAction::Eat || Action == ELisAction::Farm || Action == ELisAction::Fish) ? .1f : 0.f;
    case ELisGoal::MasterCraft:
        return LisSkillFor(Action) == Profession ? .25f : 0.f;
    case ELisGoal::Prosper:
        return Action == ELisAction::Sell ? .25f : Action == ELisAction::Chop ? .1f : Action == ELisAction::Fish ? .05f : 0.f;
    case ELisGoal::SupportCommunity:
        return Action == ELisAction::GiveHelp ? .3f : (Action == ELisAction::Farm || Action == ELisAction::Fish || Action == ELisAction::Patrol) ? .15f : 0.f;
    case ELisGoal::Discover:
        return Action == ELisAction::Explore ? .35f : 0.f;
    default:
        return 0.f;
    }
}

float Interpret(const FLisNpc& Npc, const FLisEventRecord& E)
{
    const int32 Self = Npc.Identity.Id;
    const bool bTarget = E.Target == Self;
    const bool bActor = E.Actor == Self;
    const FLisPersonality& P = Npc.Personality;
    float V = 0.f;
    switch (E.Type)
    {
    case ELisEvent::FoodShortage: V = -(.4f + .4f * P[ELisTrait::Caution] + .2f * P[ELisTrait::Empathy]); break;
    case ELisEvent::ResourceFound: V = .2f + .6f * P[ELisTrait::Curiosity]; break;
    case ELisEvent::ResourceDepleted: V = -.3f; break;
    case ELisEvent::ResourceRemoved: V = LisWorkplace(Npc.Identity.Profession) == E.Place ? -(.4f + .4f * P[ELisTrait::Aggression]) : -.1f; break;
    case ELisEvent::BlockPlaced: V = .1f; break;
    case ELisEvent::HelpGiven: V = bTarget ? .9f : bActor ? .4f + .4f * P[ELisTrait::Empathy] : .2f + .6f * P[ELisTrait::Empathy] - .3f * P[ELisTrait::Ambition]; break;
    case ELisEvent::HelpRefused: V = bTarget ? -.8f : -.2f * P[ELisTrait::Empathy]; break;
    case ELisEvent::TradeSucceeded: V = .3f; break;
    case ELisEvent::TradeFailed: V = bActor ? -.8f : -.1f; break;
    case ELisEvent::Conflict: V = (bTarget || bActor) ? -.7f + .5f * P[ELisTrait::Aggression] : -.4f * P[ELisTrait::Caution] - .3f * P[ELisTrait::Empathy] + .4f * P[ELisTrait::Aggression]; break;
    case ELisEvent::Danger: V = -(.3f + .5f * P[ELisTrait::Caution] - .3f * P[ELisTrait::Courage]) * (bTarget ? 1.5f : 1.f); break;
    case ELisEvent::DangerRepelled: V = .3f + .4f * P[ELisTrait::Loyalty]; break;
    default: break;
    }
    return FMath::Clamp(V, -1.f, 1.f);
}

void GoalScores(const FLisSimulation& Sim, const FLisNpc& Npc, float Out[LisNum<ELisGoal>()])
{
    const FLisPersonality& P = Npc.Personality;
    const float Scarcity = Sim.CommunityConcern(Npc);
    const bool bFound = Npc.Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::ResourceFound; });
    Out[LisIdx(ELisGoal::Survive)] = .2f + .35f * Npc.Need(ELisNeed::Food) + .15f * P[ELisTrait::Caution] + .3f * Scarcity * (1.f - P[ELisTrait::Empathy]);
    Out[LisIdx(ELisGoal::MasterCraft)] = .25f + .35f * P[ELisTrait::Ambition] + .3f * Npc.Skill[LisIdx(Npc.Identity.Profession)];
    Out[LisIdx(ELisGoal::Prosper)] = .2f + .4f * P[ELisTrait::Ambition] + .2f * (1.f - FMath::Min(Npc.Coins / 20.f, 1.f)) + .15f * (1.f - P[ELisTrait::Empathy])
        - .3f * FMath::Max(0.f, -Npc.Expectation[LisIdx(ELisAction::Sell)]);
    Out[LisIdx(ELisGoal::SupportCommunity)] = .15f + .3f * P[ELisTrait::Empathy] + .25f * P[ELisTrait::Loyalty] + .4f * Scarcity * P[ELisTrait::Empathy];
    Out[LisIdx(ELisGoal::Discover)] = .1f + .45f * P[ELisTrait::Curiosity] + .2f * P[ELisTrait::Courage] + (bFound ? .2f : 0.f)
        - .3f * Npc.Beliefs[LisIdx(ELisPlace::Wilds)].Risk * P[ELisTrait::Caution];
}

ELisGoal BestGoal(const float Scores[LisNum<ELisGoal>()])
{
    int32 Best = 0;
    for (int32 G = 1; G < LisNum<ELisGoal>(); ++G)
    {
        if (Scores[G] > Scores[Best])
        {
            Best = G;
        }
    }
    return static_cast<ELisGoal>(Best);
}
}

FLisSimulation::FLisSimulation(uint32 InSeed)
    : Seed(InSeed)
{
    auto Set = [this](ELisPlace Place, float X, float Y, float Stock, float Capacity, float Regen, float Danger)
    {
        FLisPlaceState& P = World.Place(Place);
        P.Pos = FVector2f(X, Y);
        P.Cell = FIntVector(FMath::RoundToInt(X * 32.f), FMath::RoundToInt(Y * 32.f), 0);
        P.Stock = Stock;
        P.Capacity = Capacity;
        P.Regen = Regen;
        P.Danger = P.BaseDanger = Danger;
    };
    Set(ELisPlace::Village, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    Set(ELisPlace::Fields, 1.5f, 0.f, 60.f, 80.f, 1.2f, 0.f);
    Set(ELisPlace::Forest, 3.f, 1.5f, 120.f, 150.f, .3f, .03f);
    Set(ELisPlace::River, 0.f, 2.f, 30.f, 40.f, .8f, .01f);
    Set(ELisPlace::Market, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    Set(ELisPlace::Wilds, 5.f, 4.f, 0.f, 60.f, .1f, .08f);
}

float FLisSimulation::Rand(int32 Actor, uint32 Salt) const
{
    return Hash01(Seed, Hour, Actor, Salt);
}

int32 FLisSimulation::AddNpc(ELisProfession Profession, ELisPlace Place)
{
    const int32 Index = Npcs.Num();
    FLisNpc& Npc = Npcs.AddDefaulted_GetRef();
    Npc.Identity.Id = Index + 1;
    auto Gen = [this, Index](uint32 Salt) { return Hash01(Seed, GenerationHour, Index, Salt); };

    static const TCHAR* First[] = { TEXT("Ar"), TEXT("El"), TEXT("Mi"), TEXT("Tor"), TEXT("Ka"), TEXT("Lin"), TEXT("Bre"), TEXT("Os"), TEXT("Va"), TEXT("Ren"), TEXT("Su"), TEXT("Dar"), TEXT("Ili"), TEXT("Gor"), TEXT("Fen"), TEXT("Ny") };
    static const TCHAR* Last[] = { TEXT("a"), TEXT("en"), TEXT("ric"), TEXT("ia"), TEXT("on"), TEXT("eth"), TEXT("is"), TEXT("or"), TEXT("wyn"), TEXT("an") };
    static const TCHAR* Origins[] = { TEXT("Elden"), TEXT("Vale Norte"), TEXT("Costa Cinzenta"), TEXT("Montes de Aethra") };
    // Re-roll (deterministically) until the name is unique in the village.
    for (uint32 Attempt = 0; Attempt < 16; ++Attempt)
    {
        Npc.Identity.Name = FString(First[static_cast<int32>(Gen(1 + 200 * Attempt) * UE_ARRAY_COUNT(First))]) + Last[static_cast<int32>(Gen(2 + 200 * Attempt) * UE_ARRAY_COUNT(Last))];
        const FString& Name = Npc.Identity.Name;
        if (!Npcs.ContainsByPredicate([&Npc, &Name](const FLisNpc& Other) { return &Other != &Npc && Other.Identity.Name == Name; }))
        {
            break;
        }
    }
    Npc.Identity.Age = 16 + static_cast<int32>(Gen(3) * 50.f);
    Npc.Identity.Profession = Profession;
    Npc.Identity.Origin = Origins[Gen(4) < .7f ? 0 : 1 + static_cast<int32>(Gen(5) * 3.f)];

    for (int32 T = 0; T < LisNum<ELisTrait>(); ++T)
    {
        const float Mean = (Gen(10 + T) + Gen(30 + T) + Gen(50 + T)) / 3.f;
        Npc.Personality.Value[T] = FMath::Clamp(.5f + (Mean - .5f) * 1.8f, .05f, .95f);
    }
    // People tend to choose a trade that suits them.
    FLisPersonality& P = Npc.Personality;
    switch (Profession)
    {
    case ELisProfession::Guard: P[ELisTrait::Courage] = FMath::Min(.95f, P[ELisTrait::Courage] + .15f); break;
    case ELisProfession::Explorer: P[ELisTrait::Curiosity] = FMath::Min(.95f, P[ELisTrait::Curiosity] + .2f); break;
    case ELisProfession::Merchant: P[ELisTrait::Ambition] = FMath::Min(.95f, P[ELisTrait::Ambition] + .15f); break;
    default: break;
    }

    for (int32 N = 0; N < NumNeeds; ++N)
    {
        Npc.Needs[N] = Gen(70 + N) * .3f;
    }
    for (float& Skill : Npc.Skill)
    {
        Skill = .05f;
    }
    Npc.Skill[LisIdx(Profession)] = .3f + .3f * Gen(80);
    Npc.Coins = 3.f + 6.f * Gen(81) + (Profession == ELisProfession::Merchant ? 8.f : 0.f);
    Npc.Food = 1.f + 2.f * Gen(82);
    Npc.Wood = Profession == ELisProfession::Woodcutter ? 2.f : 0.f;

    for (int32 L = 0; L < LisNum<ELisPlace>(); ++L)
    {
        FLisPlaceBelief& B = Npc.Beliefs[L];
        const ELisPlace Which = static_cast<ELisPlace>(L);
        B.bKnown = Which != ELisPlace::Wilds || Profession == ELisProfession::Explorer;
        B.Stock = Which == ELisPlace::Village ? World.Granary : Which == ELisPlace::Wilds ? 0.f : World.Places[L].Stock;
        B.Risk = World.Places[L].BaseDanger;
        B.Confidence = .8f;
        B.Hour = Hour;
    }
    Npc.BelievedFoodPrice = World.FoodPrice;
    Npc.Place = Place;
    Npc.Tier = TierOf(Place);
    Npc.NextThink = Hour;

    float Scores[LisNum<ELisGoal>()];
    GoalScores(*this, Npc, Scores);
    Npc.Goal = BestGoal(Scores);
    return Npc.Identity.Id;
}

FLisNpc* FLisSimulation::Find(int32 Id)
{
    return Npcs.IsValidIndex(Id - 1) ? &Npcs[Id - 1] : nullptr;
}

const FLisNpc* FLisSimulation::Find(int32 Id) const
{
    return Npcs.IsValidIndex(Id - 1) ? &Npcs[Id - 1] : nullptr;
}

int32 FLisSimulation::TravelHours(ELisPlace From, ELisPlace To) const
{
    return From == To ? 0 : FMath::Max(1, FMath::RoundToInt((World.Place(From).Pos - World.Place(To).Pos).Size()));
}

float FLisSimulation::CommunityConcern(const FLisNpc& Npc) const
{
    const float Comfortable = 1.5f * FMath::Max(1, Npcs.Num());
    return FMath::Clamp(1.f - Npc.Beliefs[LisIdx(ELisPlace::Village)].Stock / Comfortable, 0.f, 1.f);
}

ELisTier FLisSimulation::TierOf(ELisPlace Place) const
{
    const float Distance = (World.Place(Place).Pos - World.Place(Config.Focus).Pos).Size();
    return Distance <= Config.NearRadius ? ELisTier::Near : Distance <= Config.MidRadius ? ELisTier::Mid : ELisTier::Far;
}

bool FLisSimulation::IsPresent(const FLisNpc& Npc, ELisPlace Place) const
{
    return Npc.Place == Place && Npc.Current != ELisAction::Travel;
}

void FLisSimulation::Tick(int32 Hours)
{
    for (int32 H = 0; H < Hours; ++H)
    {
        const double Start = FPlatformTime::Seconds();
        TickHour();
        Stats.LastTickMs = (FPlatformTime::Seconds() - Start) * 1000.0;
        Stats.TotalTickMs += Stats.LastTickMs;
        Stats.MaxTickMs = FMath::Max(Stats.MaxTickMs, Stats.LastTickMs);
    }
}

void FLisSimulation::TickHour()
{
    ++Hour;
    ++Stats.Ticks;

    for (FLisPlaceState& P : World.Places)
    {
        P.Stock = FMath::Min(P.Capacity, P.Stock + P.Regen);
        P.Danger += (P.BaseDanger - P.Danger) * .02f;
    }
    const float Comfortable = 1.5f * FMath::Max(1, Npcs.Num());
    World.FoodPrice = 2.f * (1.f + FMath::Clamp((Comfortable - World.Granary) / Comfortable, 0.f, 1.f));

    for (FLisNpc& Npc : Npcs)
    {
        Npc.Need(ELisNeed::Food) += .03f;
        Npc.Need(ELisNeed::Rest) += Npc.Current == ELisAction::Sleep ? -.14f : .025f;
        Npc.Need(ELisNeed::Work) += IsWork(Npc.Current) ? 0.f : .02f;
        Npc.Need(ELisNeed::Social) += .012f;
        Npc.Need(ELisNeed::Purpose) += .008f;
        Npc.Need(ELisNeed::Safety) -= .04f;
        ClampNeeds(Npc);
    }

    int32 Budget = Config.MaxDecisionsPerTick;
    const int32 Count = Npcs.Num();
    for (int32 Step = 0; Step < Count; ++Step)
    {
        // Rotate the starting NPC so a tight budget does not starve the same NPCs.
        FLisNpc& Npc = Npcs[(Step + static_cast<int32>(Hour % FMath::Max(1, Count))) % Count];
        if (Npc.Current != ELisAction::Travel)
        {
            Npc.Tier = TierOf(Npc.Place);
        }
        if (Npc.Tier == ELisTier::Far)
        {
            if (Hour % Config.FarStepHours == 0)
            {
                Aggregate(Npc);
            }
            continue;
        }
        if (Hour % 24 == 6)
        {
            EvaluateGoal(Npc);
        }
        if (Npc.Current != ELisAction::Idle && Npc.BusyUntil <= Hour)
        {
            Complete(Npc);
        }
        if (Npc.Current == ELisAction::Idle && Npc.NextThink <= Hour)
        {
            if (Budget > 0)
            {
                Decide(Npc);
                --Budget;
            }
            else
            {
                ++Stats.Deferred;
            }
        }
    }
}

TArray<FLisOption> FLisSimulation::Evaluate(const FLisNpc& Npc) const
{
    TArray<FLisOption> Options;
    auto Add = [&](ELisAction Action, ELisPlace Place, int32 Target)
    {
        Options.Add({ Action, Place, Target, Utility(Npc, Action, Place, Target) });
    };
    const FLisPlaceBelief* B = Npc.Beliefs;
    // Locals know that fields, forest and river regrow; the last seen stock is aged by that rate.
    auto Expected = [this, B](ELisPlace Place)
    {
        const FLisPlaceState& P = World.Place(Place);
        return FMath::Min(P.Capacity, B[LisIdx(Place)].Stock + P.Regen * static_cast<float>(Hour - B[LisIdx(Place)].Hour));
    };

    Add(ELisAction::Idle, Npc.Place, LisNobody);
    if (Npc.Food >= 1.f)
    {
        Add(ELisAction::Eat, Npc.Place, LisNobody);
    }
    else if (B[LisIdx(ELisPlace::Village)].Stock >= 1.f)
    {
        Add(ELisAction::Eat, ELisPlace::Village, LisNobody);
    }
    Add(ELisAction::Sleep, ELisPlace::Village, LisNobody);
    if (Expected(ELisPlace::Fields) >= 1.f)
    {
        Add(ELisAction::Farm, ELisPlace::Fields, LisNobody);
    }
    Add(ELisAction::Fish, ELisPlace::River, LisNobody);
    if (Expected(ELisPlace::Forest) >= 1.f)
    {
        Add(ELisAction::Chop, ELisPlace::Forest, LisNobody);
    }
    if (Npc.Wood >= 1.f || Npc.Food >= 4.f)
    {
        Add(ELisAction::Sell, ELisPlace::Market, LisNobody);
    }
    if (Npc.Coins >= Npc.BelievedFoodPrice)
    {
        Add(ELisAction::BuyFood, ELisPlace::Market, LisNobody);
    }
    if (Npc.Food < 1.f)
    {
        if (const int32 Helper = FindHelper(Npc))
        {
            Add(ELisAction::AskHelp, ELisPlace::Village, Helper);
        }
    }
    if (Npc.Food >= 3.f)
    {
        if (const int32 Needy = FindNeedy(Npc))
        {
            Add(ELisAction::GiveHelp, Npc.Place, Needy);
        }
    }
    Add(ELisAction::Socialize, ELisPlace::Village, FindCompany(Npc, ELisPlace::Village));
    Add(ELisAction::Explore, ELisPlace::Wilds, LisNobody);
    if (Npc.Identity.Profession == ELisProfession::Guard || Npc.Trait(ELisTrait::Courage) > .65f)
    {
        ELisPlace Riskiest = ELisPlace::Fields;
        for (ELisPlace Candidate : { ELisPlace::Forest, ELisPlace::Wilds })
        {
            if (B[LisIdx(Candidate)].bKnown && B[LisIdx(Candidate)].Risk > B[LisIdx(Riskiest)].Risk)
            {
                Riskiest = Candidate;
            }
        }
        Add(ELisAction::Patrol, Riskiest, LisNobody);
    }
    return Options;
}

float FLisSimulation::Utility(const FLisNpc& Npc, ELisAction Action, ELisPlace Place, int32 Target) const
{
    const FLisPersonality& P = Npc.Personality;
    const int32 A = LisIdx(Action);

    float U = 0.f;
    for (int32 N = 0; N < NumNeeds; ++N)
    {
        U += FMath::Pow(Npc.Needs[N], 1.5f) * Satisfies[A][N];
    }

    const ELisProfession SkillFor = LisSkillFor(Action);
    if (SkillFor != ELisProfession::Count)
    {
        U = SkillFor == Npc.Identity.Profession ? U * 1.3f + .12f : U * .7f;
        U += .15f * Npc.Skill[LisIdx(SkillFor)];
    }

    const float Concern = CommunityConcern(Npc);
    const float CoinNeed = 1.f - FMath::Min(Npc.Coins / 15.f, 1.f);
    switch (Action)
    {
    case ELisAction::Idle:
        U += .02f;
        break;
    case ELisAction::Farm:
    case ELisAction::Fish:
        U += Concern * (.25f * P[ELisTrait::Loyalty] + .3f * P[ELisTrait::Empathy]);
        break;
    case ELisAction::Chop:
        U += .1f * CoinNeed * P[ELisTrait::Ambition];
        break;
    case ELisAction::Sell:
        U += .25f * CoinNeed * (.5f + P[ELisTrait::Ambition]) + .1f * FMath::Min(Npc.Wood / 4.f, 1.f);
        break;
    case ELisAction::BuyFood:
        U = U * (.7f + .3f * P[ELisTrait::Caution]) + Concern * .2f * P[ELisTrait::Caution] - .15f * (Npc.BelievedFoodPrice - 2.f) / 2.f;
        break;
    case ELisAction::AskHelp:
    {
        const FLisRelation* R = Npc.FindRelation(Target);
        const float Bond = R ? (R->Trust + R->Affection) * .5f : .5f;
        U *= (.4f + .6f * P[ELisTrait::Sociability]) * (1.2f - .7f * P[ELisTrait::Ambition]) * (.5f + Bond);
        break;
    }
    case ELisAction::GiveHelp:
        U = U * (.2f + 1.5f * P[ELisTrait::Empathy]) + Concern * .3f * P[ELisTrait::Empathy];
        if (const FLisNpc* Needy = Find(Target))
        {
            U += .5f * Needy->Need(ELisNeed::Food) * P[ELisTrait::Empathy];
        }
        break;
    case ELisAction::Socialize:
        U *= .4f + .9f * P[ELisTrait::Sociability];
        break;
    case ELisAction::Explore:
    {
        U *= .3f + .9f * P[ELisTrait::Curiosity] + .3f * P[ELisTrait::Courage];
        const FLisPlaceBelief& Wilds = Npc.Beliefs[LisIdx(ELisPlace::Wilds)];
        if (Wilds.bKnown && Wilds.Stock >= 5.f)
        {
            U += .5f * FMath::Pow(Npc.Need(ELisNeed::Food), 1.5f);
        }
        U += Concern * .2f * P[ELisTrait::Curiosity];
        break;
    }
    case ELisAction::Patrol:
        U *= .3f + .4f * P[ELisTrait::Loyalty] + .4f * P[ELisTrait::Courage];
        break;
    default:
        break;
    }

    // Learned risk of the destination: caution amplifies it, courage dampens it; guards are drawn to it.
    if (IsOutdoor(Place))
    {
        const float Risk = Npc.Beliefs[LisIdx(Place)].Risk;
        const float Duty = Npc.Identity.Profession == ELisProfession::Guard ? 1.f : .4f;
        U += Action == ELisAction::Patrol
            ? Duty * Risk * (.5f + P[ELisTrait::Courage] - P[ELisTrait::Caution])
            : -2.f * Risk * FMath::Max(0.f, .3f + P[ELisTrait::Caution] - .5f * P[ELisTrait::Courage]);
    }

    U += GoalBonus(Npc.Goal, Action, Npc.Identity.Profession);
    U += .25f * Npc.Expectation[A];
    U -= .025f * TravelHours(Npc.Place, Place);

    const int32 Clock = static_cast<int32>(Hour % 24);
    if (Clock >= 21 || Clock < 5)
    {
        U += Action == ELisAction::Sleep ? .1f + .25f * Npc.Need(ELisNeed::Rest) : IsWork(Action) ? -.1f : 0.f;
    }
    // Controlled, seeded variation: breaks ties, never replaces the reasons above.
    U += (Rand(Npc.Identity.Id, 100 + A) - .5f) * .04f;
    return U;
}

int32 FLisSimulation::FindHelper(const FLisNpc& Npc) const
{
    int32 Best = LisNobody;
    float BestBond = .9f; // only people the NPC regards above neutral
    for (const TPair<int32, FLisRelation>& Pair : Npc.Relations)
    {
        const float Bond = Pair.Value.Trust + Pair.Value.Affection - Pair.Value.Resentment;
        if (Pair.Key > 0 && Pair.Key != Npc.Identity.Id && Bond > BestBond)
        {
            BestBond = Bond;
            Best = Pair.Key;
        }
    }
    return Best;
}

int32 FLisSimulation::FindNeedy(const FLisNpc& Npc) const
{
    // Only people the NPC can see right now: no global knowledge of who is hungry.
    int32 Best = LisNobody;
    float BestScore = 0.f;
    for (const FLisNpc& Other : Npcs)
    {
        if (Other.Identity.Id == Npc.Identity.Id || !IsPresent(Other, Npc.Place) || Other.Food >= 1.f || Other.Need(ELisNeed::Food) <= .6f)
        {
            continue;
        }
        const FLisRelation* R = Npc.FindRelation(Other.Identity.Id);
        const float Score = Other.Need(ELisNeed::Food) * (R ? .5f + R->Affection : 1.f);
        if (Score > BestScore)
        {
            BestScore = Score;
            Best = Other.Identity.Id;
        }
    }
    return Best;
}

int32 FLisSimulation::FindCompany(const FLisNpc& Npc, ELisPlace Place) const
{
    int32 Best = LisNobody;
    float BestScore = -1.f;
    for (const FLisNpc& Other : Npcs)
    {
        if (Other.Identity.Id == Npc.Identity.Id || !IsPresent(Other, Place))
        {
            continue;
        }
        const FLisRelation* R = Npc.FindRelation(Other.Identity.Id);
        const float Score = R ? R->Affection - R->Resentment : .5f;
        if (Score > BestScore)
        {
            BestScore = Score;
            Best = Other.Identity.Id;
        }
    }
    return Best;
}

FLisOption FLisSimulation::Decide(FLisNpc& Npc)
{
    const TArray<FLisOption> Options = Evaluate(Npc);
    FLisOption Best = Options[0];
    for (const FLisOption& Option : Options)
    {
        if (Option.Score > Best.Score)
        {
            Best = Option;
        }
    }
    ++Npc.Decisions;
    ++Stats.Decisions;
    Npc.Intent = Best.Action;
    Npc.IntentPlace = Best.Place;
    Npc.IntentTarget = Best.Target;
    if (Npc.Place != Best.Place)
    {
        Npc.Current = ELisAction::Travel;
        Npc.BusyUntil = Hour + TravelHours(Npc.Place, Best.Place);
        ++Npc.ActionCount[LisIdx(ELisAction::Travel)];
    }
    else
    {
        StartIntent(Npc);
    }
    return Best;
}

void FLisSimulation::StartIntent(FLisNpc& Npc)
{
    // Plans are checked against the world once the NPC can see it; a stale belief causes a replan.
    const FLisPlaceState& P = World.Place(Npc.Place);
    bool bValid = true;
    switch (Npc.Intent)
    {
    case ELisAction::Eat: bValid = Npc.Food >= 1.f || (Npc.Place == ELisPlace::Village && World.Granary >= 1.f); break;
    case ELisAction::Farm:
    case ELisAction::Chop: bValid = P.Stock >= 1.f; break;
    case ELisAction::Sell: bValid = Npc.Wood >= 1.f || Npc.Food >= 4.f; break;
    case ELisAction::BuyFood: bValid = Npc.Coins >= World.FoodPrice; break;
    case ELisAction::AskHelp:
    case ELisAction::GiveHelp:
    {
        const FLisNpc* Other = Find(Npc.IntentTarget);
        bValid = Other && IsPresent(*Other, Npc.Place);
        break;
    }
    default: break;
    }
    if (!bValid)
    {
        ++Npc.Replans;
        ++Stats.Replans;
        Npc.Current = ELisAction::Idle;
        Npc.NextThink = Hour;
        return;
    }
    const int32 Duration = Durations[LisIdx(Npc.Intent)] * (Npc.Tier == ELisTier::Mid ? 2 : 1);
    Npc.Current = Npc.Intent;
    Npc.BusyUntil = Hour + Duration;
    ++Npc.ActionCount[LisIdx(Npc.Intent)];
}

void FLisSimulation::Complete(FLisNpc& Npc)
{
    const ELisAction Action = Npc.Current;
    Npc.Current = ELisAction::Idle;
    Npc.NextThink = Hour;
    if (Action == ELisAction::Travel)
    {
        Npc.Place = Npc.IntentPlace;
        Npc.Tier = TierOf(Npc.Place);
        Observe(Npc);
        StartIntent(Npc);
        return;
    }

    const int32 Id = Npc.Identity.Id;
    const float Scale = Npc.Tier == ELisTier::Mid ? 2.f : 1.f;
    const FLisPersonality& Pers = Npc.Personality;
    FLisPlaceState& P = World.Place(Npc.Place);
    auto SkillOf = [&Npc](ELisProfession Which) { return Npc.Skill[LisIdx(Which)]; };

    switch (Action)
    {
    case ELisAction::Eat:
        if (Npc.Food >= 1.f)
        {
            Npc.Food -= 1.f;
            Npc.Need(ELisNeed::Food) -= .6f;
        }
        else if (Npc.Place == ELisPlace::Village && World.Granary >= 1.f)
        {
            World.Granary -= 1.f;
            Npc.Need(ELisNeed::Food) -= .6f;
        }
        break;

    case ELisAction::Farm:
    case ELisAction::Chop:
    {
        const bool bFarm = Action == ELisAction::Farm;
        const float Skill = SkillOf(bFarm ? ELisProfession::Farmer : ELisProfession::Woodcutter);
        const float Yield = FMath::Min(P.Stock, (bFarm ? 1.5f + 3.f * Skill : 1.f + 2.f * Skill) * Scale);
        P.Stock -= Yield;
        if (Yield < .5f)
        {
            Emit(ELisEvent::ResourceDepleted, Npc.Place, Id, LisNobody, P.Stock);
            Practice(Npc, Action, false);
            Learn(Npc, Action, -.6f);
            break;
        }
        if (bFarm)
        {
            // Loyal farmers bring more of the harvest to the community granary.
            const float Keep = Yield * (.25f + .35f * (1.f - Pers[ELisTrait::Loyalty]));
            Npc.Food += Keep;
            World.Granary += Yield - Keep;
        }
        else
        {
            Npc.Wood += Yield;
        }
        Npc.Need(ELisNeed::Work) -= .6f * Scale;
        Npc.Need(ELisNeed::Purpose) -= .2f;
        Practice(Npc, Action, true);
        Learn(Npc, Action, FMath::Clamp(Yield / 3.f - .3f, -1.f, 1.f));
        DangerCheck(Npc, Scale);
        break;
    }

    case ELisAction::Fish:
    {
        const float Skill = SkillOf(ELisProfession::Fisher);
        const bool bCaught = Rand(Id, 11) < .45f + .45f * Skill;
        const float Yield = bCaught ? FMath::Min(P.Stock, (1.f + 2.f * Skill) * Scale) : 0.f;
        P.Stock -= Yield;
        const float Keep = Yield * (.5f + .3f * (1.f - Pers[ELisTrait::Loyalty]));
        Npc.Food += Keep;
        World.Granary += Yield - Keep;
        Npc.Need(ELisNeed::Work) -= .6f * Scale;
        Npc.Need(ELisNeed::Purpose) -= .2f;
        Practice(Npc, Action, Yield > 0.f);
        Learn(Npc, Action, Yield > 0.f ? .4f : -.4f);
        DangerCheck(Npc, Scale);
        break;
    }

    case ELisAction::Sell:
    case ELisAction::BuyFood:
    {
        int32 Partner = LisNobody;
        for (const FLisNpc& Other : Npcs)
        {
            if (Other.Identity.Id != Id && Other.Identity.Profession == ELisProfession::Merchant && IsPresent(Other, ELisPlace::Market))
            {
                Partner = Other.Identity.Id;
                break;
            }
        }
        Npc.BelievedFoodPrice = World.FoodPrice;
        const float FailChance = World.TradeRisk + .1f - .15f * SkillOf(ELisProfession::Merchant);
        const bool bFailed = Rand(Id, 21) < FailChance;
        float Amount = 0.f;
        if (Action == ELisAction::Sell)
        {
            const float SpareFood = FMath::Max(0.f, Npc.Food - 3.f);
            const float Value = Npc.Wood * World.WoodPrice + SpareFood * World.FoodPrice * .8f;
            Amount = Value * (bFailed ? .45f : .9f + .2f * Rand(Id, 22));
            Npc.Wood = 0.f;
            Npc.Food -= SpareFood;
            Npc.Coins += Amount;
            Npc.Need(ELisNeed::Work) -= .3f;
            if (FLisNpc* Merchant = Find(Partner))
            {
                Merchant->Coins += Value * .1f;
            }
        }
        else
        {
            const int32 Units = FMath::Min(3, FMath::FloorToInt(Npc.Coins / World.FoodPrice));
            if (Units < 1)
            {
                ++Npc.Replans;
                ++Stats.Replans;
                break;
            }
            Npc.Coins -= Units * World.FoodPrice;
            Amount = bFailed ? Units * .5f : static_cast<float>(Units);
            Npc.Food += Amount;
        }
        Emit(bFailed ? ELisEvent::TradeFailed : ELisEvent::TradeSucceeded, ELisPlace::Market, Id, Partner, Amount);
        Practice(Npc, Action, !bFailed);
        Learn(Npc, Action, bFailed ? -1.f : .5f);
        break;
    }

    case ELisAction::AskHelp:
    {
        FLisNpc* Helper = Find(Npc.IntentTarget);
        if (!Helper || !IsPresent(*Helper, Npc.Place))
        {
            Learn(Npc, Action, -.2f);
            break;
        }
        const FLisPersonality& H = Helper->Personality;
        const FLisRelation* R = Helper->FindRelation(Id);
        const FLisRelation Bond = R ? *R : FLisRelation();
        const float Willing = .35f * H[ELisTrait::Empathy] + .3f * Bond.Affection + .2f * Bond.Trust + .15f * H[ELisTrait::Loyalty]
            - .25f * H[ELisTrait::Ambition] - .3f * Bond.Resentment;
        if (Helper->Food >= 2.f && Willing > .35f)
        {
            const float Gift = FMath::Min(2.f, Helper->Food - 1.f);
            Helper->Food -= Gift;
            Npc.Food += Gift;
            Emit(ELisEvent::HelpGiven, Npc.Place, Helper->Identity.Id, Id, Gift);
            Learn(Npc, Action, .6f);
        }
        else
        {
            Emit(ELisEvent::HelpRefused, Npc.Place, Helper->Identity.Id, Id, 0.f);
            Learn(Npc, Action, -.6f);
        }
        Npc.Need(ELisNeed::Social) -= .2f;
        break;
    }

    case ELisAction::GiveHelp:
    {
        FLisNpc* Needy = Find(Npc.IntentTarget);
        if (Needy && IsPresent(*Needy, Npc.Place) && Npc.Food >= 2.f)
        {
            const float Gift = FMath::Min(2.f, Npc.Food - 1.f);
            Npc.Food -= Gift;
            Needy->Food += Gift;
            Emit(ELisEvent::HelpGiven, Npc.Place, Id, Needy->Identity.Id, Gift);
            Npc.Need(ELisNeed::Purpose) -= .4f;
            Npc.Need(ELisNeed::Social) -= .2f;
            Learn(Npc, Action, .5f);
        }
        break;
    }

    case ELisAction::Socialize:
    {
        FLisNpc* Other = Find(Npc.IntentTarget);
        if (!Other || !IsPresent(*Other, Npc.Place))
        {
            Other = Find(FindCompany(Npc, Npc.Place));
        }
        if (!Other)
        {
            Npc.Need(ELisNeed::Social) -= .1f;
            break;
        }
        Npc.Need(ELisNeed::Social) -= .6f * Scale;
        Other->Need(ELisNeed::Social) -= .3f;
        if (Npc.Tier == ELisTier::Near)
        {
            ShareKnowledge(Npc, *Other);
            ShareKnowledge(*Other, Npc);
        }
        FLisRelation& Mine = Npc.Relation(Other->Identity.Id);
        Mine.Affection += .03f * (.5f + Pers[ELisTrait::Sociability]);
        Mine.Trust += .01f;
        ClampRelation(Mine);
        FLisRelation& Theirs = Other->Relation(Id);
        Theirs.Affection += .02f * (.5f + Other->Trait(ELisTrait::Sociability));
        ClampRelation(Theirs);
        if (Mine.Resentment > .45f && Pers[ELisTrait::Aggression] > .6f && Rand(Id, 31) < Pers[ELisTrait::Aggression])
        {
            Emit(ELisEvent::Conflict, Npc.Place, Id, Other->Identity.Id, 1.f);
        }
        break;
    }

    case ELisAction::Explore:
    {
        FLisPlaceBelief& Belief = Npc.Beliefs[LisIdx(ELisPlace::Wilds)];
        bool bFound = false;
        if (P.Stock >= 5.f && Belief.Stock < 5.f)
        {
            Emit(ELisEvent::ResourceFound, ELisPlace::Wilds, Id, LisNobody, P.Stock);
            bFound = true;
        }
        const float Yield = FMath::Min(P.Stock, (1.f + 1.5f * SkillOf(ELisProfession::Explorer)) * Scale);
        P.Stock -= Yield;
        Npc.Food += Yield;
        Belief.Stock = P.Stock;
        Belief.Hour = Hour;
        Npc.Need(ELisNeed::Purpose) -= .6f;
        Npc.Need(ELisNeed::Work) -= .4f;
        Practice(Npc, Action, Yield > 0.f);
        Learn(Npc, Action, bFound ? .8f : Yield > 0.f ? .2f : -.3f);
        DangerCheck(Npc, 1.5f * Scale);
        break;
    }

    case ELisAction::Patrol:
    {
        P.Danger = FMath::Max(P.BaseDanger * .5f, P.Danger * .6f);
        if (Rand(Id, 51) < FMath::Min(.9f, P.Danger * 4.f + .05f))
        {
            const bool bRepelled = Rand(Id, 52) < .35f + .3f * Pers[ELisTrait::Courage] + .35f * SkillOf(ELisProfession::Guard);
            if (bRepelled)
            {
                P.Danger = P.BaseDanger * .5f;
                Emit(ELisEvent::DangerRepelled, Npc.Place, Id, LisNobody, 1.f);
            }
            else
            {
                Emit(ELisEvent::Danger, Npc.Place, LisNobody, Id, 1.f);
            }
            Learn(Npc, Action, bRepelled ? .5f : -.5f);
            Practice(Npc, Action, bRepelled);
        }
        else
        {
            Practice(Npc, Action, true);
        }
        Npc.Need(ELisNeed::Work) -= .6f * Scale;
        Npc.Need(ELisNeed::Purpose) -= .3f;
        break;
    }

    default:
        break;
    }
    ClampNeeds(Npc);
    Observe(Npc);
}

void FLisSimulation::DangerCheck(FLisNpc& Npc, float Scale)
{
    const FLisPlaceState& P = World.Place(Npc.Place);
    if (Rand(Npc.Identity.Id, 41) < P.Danger * 3.f * Scale)
    {
        Npc.Wood *= .5f;
        Emit(ELisEvent::Danger, Npc.Place, LisNobody, Npc.Identity.Id, 1.f);
    }
    else
    {
        // A safe trip slowly lowers the learned risk of the place.
        Npc.Beliefs[LisIdx(Npc.Place)].Risk *= .97f;
    }
}

void FLisSimulation::Aggregate(FLisNpc& Npc)
{
    // Far tier: one coarse step replaces many decisions. Production and consumption only.
    ++Stats.AggregateSteps;
    const float Hours = static_cast<float>(Config.FarStepHours);
    float Meals = Npc.Need(ELisNeed::Food) / .6f;
    const float FromPack = FMath::Min(Npc.Food, Meals);
    Npc.Food -= FromPack;
    Meals -= FromPack;
    World.Granary = FMath::Max(0.f, World.Granary - Meals);
    Npc.Need(ELisNeed::Food) = .1f;
    Npc.Need(ELisNeed::Rest) = .2f;
    Npc.Need(ELisNeed::Work) = .1f;
    Npc.Need(ELisNeed::Social) *= .6f;
    Npc.Need(ELisNeed::Safety) *= .5f;

    const float Skill = Npc.Skill[LisIdx(Npc.Identity.Profession)];
    const float Shifts = Hours / 6.f;
    switch (Npc.Identity.Profession)
    {
    case ELisProfession::Farmer:
    case ELisProfession::Fisher:
    {
        FLisPlaceState& P = World.Place(LisWorkplace(Npc.Identity.Profession));
        const float Yield = FMath::Min(P.Stock, (1.f + 2.5f * Skill) * Shifts);
        P.Stock -= Yield;
        World.Granary += Yield;
        break;
    }
    case ELisProfession::Woodcutter:
    {
        FLisPlaceState& P = World.Place(ELisPlace::Forest);
        const float Yield = FMath::Min(P.Stock, (1.f + 2.f * Skill) * Shifts);
        P.Stock -= Yield;
        Npc.Wood += Yield;
        break;
    }
    case ELisProfession::Merchant:
        Npc.Coins += Shifts;
        break;
    default:
        break;
    }
    float& Practiced = Npc.Skill[LisIdx(Npc.Identity.Profession)];
    Practiced += .01f * Shifts * (1.f - Practiced);
}

void FLisSimulation::Learn(FLisNpc& Npc, ELisAction Action, float Outcome)
{
    float& E = Npc.Expectation[LisIdx(Action)];
    E += .25f * (FMath::Clamp(Outcome, -1.f, 1.f) - E);
}

void FLisSimulation::Practice(FLisNpc& Npc, ELisAction Action, bool bSuccess)
{
    const ELisProfession Which = LisSkillFor(Action);
    if (Which != ELisProfession::Count)
    {
        float& Skill = Npc.Skill[LisIdx(Which)];
        Skill += (bSuccess ? .03f : .008f) * (1.f - Skill);
    }
}

void FLisSimulation::Observe(FLisNpc& Npc)
{
    FLisPlaceBelief& B = Npc.Beliefs[LisIdx(Npc.Place)];
    B.bKnown = true;
    B.Confidence = 1.f;
    B.Hour = Hour;
    B.Source = ELisSource::Observed;
    switch (Npc.Place)
    {
    case ELisPlace::Village: B.Stock = World.Granary; break;
    case ELisPlace::Market: Npc.BelievedFoodPrice = World.FoodPrice; break;
    case ELisPlace::Wilds: break; // what grows in the wilds is only found by exploring
    default: B.Stock = World.Place(Npc.Place).Stock; break;
    }
}

int32 FLisSimulation::Emit(ELisEvent Type, ELisPlace Place, int32 Actor, int32 Target, float Amount, FIntVector Cell)
{
    FLisEventRecord E;
    E.Id = NextEventId++;
    E.Type = Type;
    E.Place = Place;
    E.Hour = Hour;
    E.Actor = Actor;
    E.Target = Target;
    E.Amount = Amount;
    E.Cell = Cell;
    Events.Add(E);
    if (Events.Num() > Config.EventLogCapacity)
    {
        Events.RemoveAt(0, Events.Num() - Config.EventLogCapacity);
    }
    ++Stats.Events;
    for (FLisNpc& Npc : Npcs)
    {
        const int32 Id = Npc.Identity.Id;
        if (IsPresent(Npc, Place) || Id == Actor || Id == Target)
        {
            Remember(Npc, E, ELisSource::Observed, LisNobody, 1.f);
        }
    }
    return E.Id;
}

int32 FLisSimulation::EmitAtCell(ELisEvent Type, ELisPlace Place, int32 Actor, float Amount, FIntVector Cell)
{
    FLisEventRecord E;
    E.Id = NextEventId++;
    E.Type = Type;
    E.Place = Place;
    E.Hour = Hour;
    E.Actor = Actor;
    E.Amount = Amount;
    E.Cell = Cell;
    Events.Add(E);
    if (Events.Num() > Config.EventLogCapacity)
    {
        Events.RemoveAt(0, Events.Num() - Config.EventLogCapacity);
    }
    ++Stats.Events;
    const FIntVector Offset = World.Place(Place).Cell - Cell;
    const bool bInRange = FVector(Offset.X, Offset.Y, 0.f).Size() <= Config.PerceptionRadiusCells;
    for (FLisNpc& Npc : Npcs)
    {
        if ((bInRange && IsPresent(Npc, Place)) || Npc.Identity.Id == Actor)
        {
            Remember(Npc, E, ELisSource::Observed, LisNobody, 1.f);
        }
    }
    return E.Id;
}

void FLisSimulation::Remember(FLisNpc& Npc, const FLisEventRecord& E, ELisSource Source, int32 ToldBy, float Confidence)
{
    if (FLisMemory* Known = Npc.Memories.FindByPredicate([&E](const FLisMemory& M) { return M.EventId == E.Id; }))
    {
        Known->Confidence = FMath::Max(Known->Confidence, Confidence);
        return;
    }

    const int32 Self = Npc.Identity.Id;
    const FLisPersonality& P = Npc.Personality;
    const bool bTarget = E.Target == Self;
    const bool bActor = E.Actor == Self;
    const bool bWorkplace = LisWorkplace(Npc.Identity.Profession) == E.Place;
    // Hearsay weighs less than what the NPC saw, and less the less it trusts the teller.
    const float W = Source == ELisSource::Observed ? 1.f : .5f * Confidence;
    FLisPlaceBelief& B = Npc.Beliefs[LisIdx(E.Place)];
    auto Newer = [&B, &E]() { return E.Hour >= B.Hour; };
    auto TouchRelation = [&Npc](int32 Other, TFunctionRef<void(FLisRelation&)> Change)
    {
        if (Other != LisNobody && Other != Npc.Identity.Id)
        {
            FLisRelation& R = Npc.Relation(Other);
            Change(R);
            ClampRelation(R);
        }
    };

    switch (E.Type)
    {
    case ELisEvent::FoodShortage:
        if (Newer())
        {
            B.Stock = E.Amount;
            B.Hour = E.Hour;
            B.Source = Source;
            B.Confidence = Confidence;
        }
        Npc.Need(ELisNeed::Safety) += .1f * W * P[ELisTrait::Caution];
        break;
    case ELisEvent::ResourceFound:
        B.bKnown = true;
        B.Stock = FMath::Max(B.Stock, E.Amount * Confidence);
        B.Hour = FMath::Max(B.Hour, E.Hour);
        break;
    case ELisEvent::ResourceDepleted:
        if (Newer())
        {
            B.Stock = E.Amount;
            B.Hour = E.Hour;
        }
        break;
    case ELisEvent::ResourceRemoved:
        B.Stock = FMath::Max(0.f, B.Stock - E.Amount * W);
        if (bWorkplace)
        {
            Npc.Need(ELisNeed::Purpose) += .2f * W;
            TouchRelation(E.Actor, [&](FLisRelation& R) { R.Resentment += .15f * W * (.5f + P[ELisTrait::Aggression]); R.Trust -= .05f * W; });
        }
        break;
    case ELisEvent::BlockPlaced:
        TouchRelation(E.Actor, [&](FLisRelation& R) { R.Respect += .02f * W; });
        break;
    case ELisEvent::HelpGiven:
        if (bTarget)
        {
            TouchRelation(E.Actor, [](FLisRelation& R) { R.Trust += .15f; R.Affection += .12f; R.Respect += .05f; });
        }
        else if (bActor)
        {
            TouchRelation(E.Target, [](FLisRelation& R) { R.Affection += .04f; });
        }
        else
        {
            TouchRelation(E.Actor, [&](FLisRelation& R) { R.Respect += .06f * W * (.5f + P[ELisTrait::Empathy]); R.Trust += .03f * W; });
        }
        break;
    case ELisEvent::HelpRefused:
        if (bTarget)
        {
            TouchRelation(E.Actor, [](FLisRelation& R) { R.Resentment += .15f; R.Trust -= .1f; R.Affection -= .05f; });
        }
        else if (!bActor)
        {
            TouchRelation(E.Actor, [&](FLisRelation& R) { R.Respect -= .03f * W * P[ELisTrait::Empathy]; });
        }
        break;
    case ELisEvent::TradeSucceeded:
        if (bActor)
        {
            TouchRelation(E.Target, [](FLisRelation& R) { R.Trust += .03f; });
        }
        break;
    case ELisEvent::TradeFailed:
        if (bActor)
        {
            TouchRelation(E.Target, [&](FLisRelation& R) { R.Trust -= .12f; R.Resentment += .06f * (.5f + P[ELisTrait::Aggression]); });
        }
        break;
    case ELisEvent::Conflict:
        if (bTarget)
        {
            TouchRelation(E.Actor, [&](FLisRelation& R) { R.Resentment += .2f; R.Fear += .1f * (1.f - P[ELisTrait::Courage]); R.Affection -= .1f; });
        }
        else if (bActor)
        {
            TouchRelation(E.Target, [](FLisRelation& R) { R.Resentment += .05f; });
        }
        else
        {
            TouchRelation(E.Actor, [&](FLisRelation& R) { R.Fear += .06f * W * P[ELisTrait::Caution]; R.Respect -= .03f * W; });
        }
        break;
    case ELisEvent::Danger:
        // Learning: an observed threat raises the place's risk more than a rumour does.
        B.Risk += (Source == ELisSource::Observed ? .45f : .25f * Confidence) * (1.f - B.Risk);
        Npc.Need(ELisNeed::Safety) += bTarget ? .5f : .2f * W;
        break;
    case ELisEvent::DangerRepelled:
        B.Risk *= .85f;
        TouchRelation(E.Actor, [&](FLisRelation& R) { R.Respect += .1f * W; R.Trust += .04f * W; });
        break;
    default:
        break;
    }
    ClampNeeds(Npc);

    FLisMemory M;
    M.EventId = E.Id;
    M.Type = E.Type;
    M.Place = E.Place;
    M.Hour = E.Hour;
    M.LearnedHour = Hour;
    M.Actor = E.Actor;
    M.Target = E.Target;
    M.Amount = E.Amount;
    M.Source = Source;
    M.ToldBy = ToldBy;
    M.Confidence = Confidence;
    M.Valence = Interpret(Npc, E);
    M.Relevance = BaseRelevance[LisIdx(E.Type)] + (bTarget || bActor ? .5f : 0.f) + (bWorkplace ? .25f : 0.f);
    if (const FLisRelation* R = (E.Actor > 0 && !bActor) ? Npc.FindRelation(E.Actor) : nullptr)
    {
        M.Relevance += FMath::Abs(R->Affection - .5f) + .5f * R->Resentment;
    }
    M.Relevance = FMath::Min(M.Relevance, 2.f);
    Npc.Memories.Add(M);
    ++Stats.MemoriesFormed;
    if (Npc.Memories.Num() > Config.MemoryCapacity)
    {
        Forget(Npc);
    }

    // A threat seen at the NPC's own place interrupts what it is doing.
    if (E.Type == ELisEvent::Danger && Source == ELisSource::Observed && Npc.Place == E.Place
        && Npc.Current != ELisAction::Idle && Npc.Current != ELisAction::Travel && Npc.Current != ELisAction::Sleep)
    {
        Npc.Current = ELisAction::Idle;
        Npc.NextThink = Hour;
        ++Npc.Replans;
        ++Stats.Replans;
    }
}

void FLisSimulation::Forget(FLisNpc& Npc)
{
    int32 Weakest = 0;
    float WeakestScore = TNumericLimits<float>::Max();
    for (int32 I = 0; I < Npc.Memories.Num(); ++I)
    {
        const FLisMemory& M = Npc.Memories[I];
        const float Age = static_cast<float>(Hour - M.LearnedHour);
        const float Score = M.Relevance * M.Confidence / (1.f + Age / 72.f);
        if (Score < WeakestScore)
        {
            WeakestScore = Score;
            Weakest = I;
        }
    }
    Npc.Memories.RemoveAt(Weakest);
    ++Stats.MemoriesForgotten;
}

int32 FLisSimulation::ShareKnowledge(FLisNpc& Speaker, FLisNpc& Listener, int32 MaxItems)
{
    TArray<const FLisMemory*> Candidates;
    for (const FLisMemory& M : Speaker.Memories)
    {
        if (Hour - M.Hour <= 96 && M.Type != ELisEvent::GoalChanged && !Listener.FindMemory(M.EventId))
        {
            Candidates.Add(&M);
        }
    }
    Candidates.Sort([](const FLisMemory& A, const FLisMemory& B) { return A.Relevance * A.Confidence > B.Relevance * B.Confidence; });

    const FLisRelation* R = Listener.FindRelation(Speaker.Identity.Id);
    const float Trust = R ? R->Trust : .5f;
    const int32 Count = FMath::Min(MaxItems, Candidates.Num());
    TArray<FLisEventRecord> Told;
    TArray<float> Confidence;
    for (int32 I = 0; I < Count; ++I)
    {
        // The listener gets the speaker's version, not the objective record.
        const FLisMemory& M = *Candidates[I];
        FLisEventRecord& E = Told.AddDefaulted_GetRef();
        E.Id = M.EventId;
        E.Type = M.Type;
        E.Place = M.Place;
        E.Hour = M.Hour;
        E.Actor = M.Actor;
        E.Target = M.Target;
        E.Amount = M.Amount;
        Confidence.Add(M.Confidence * (.3f + .6f * Trust));
    }
    // Remember can reallocate the speaker's memories only if speaker == listener; copy first anyway.
    for (int32 I = 0; I < Told.Num(); ++I)
    {
        Remember(Listener, Told[I], ELisSource::Told, Speaker.Identity.Id, Confidence[I]);
    }
    return Told.Num();
}

void FLisSimulation::EvaluateGoal(FLisNpc& Npc)
{
    float Scores[LisNum<ELisGoal>()];
    GoalScores(*this, Npc, Scores);
    const ELisGoal Best = BestGoal(Scores);
    // Hysteresis: a new goal must clearly beat the current one.
    if (Best != Npc.Goal && Scores[LisIdx(Best)] > Scores[LisIdx(Npc.Goal)] + .15f)
    {
        Npc.Goal = Best;
        ++Npc.GoalChanges;
        ++Stats.GoalChanges;
        Emit(ELisEvent::GoalChanged, Npc.Place, Npc.Identity.Id, LisNobody, static_cast<float>(LisIdx(Best)));
    }
}

int32 FLisSimulation::MemoryCount() const
{
    int32 Count = 0;
    for (const FLisNpc& Npc : Npcs)
    {
        Count += Npc.Memories.Num();
    }
    return Count;
}

SIZE_T FLisSimulation::ApproxBytes() const
{
    SIZE_T Bytes = sizeof(*this) + Npcs.GetAllocatedSize() + Events.GetAllocatedSize();
    for (const FLisNpc& Npc : Npcs)
    {
        Bytes += Npc.Memories.GetAllocatedSize() + Npc.Relations.GetAllocatedSize()
            + Npc.Identity.Name.GetAllocatedSize() + Npc.Identity.Origin.GetAllocatedSize();
    }
    return Bytes;
}
