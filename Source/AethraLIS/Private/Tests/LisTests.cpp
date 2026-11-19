#if WITH_DEV_AUTOMATION_TESTS

#include "LisElden.h"
#include "LisSimulation.h"
#include "LisVoxelAdapter.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
constexpr EAutomationTestFlags LisTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

// A daytime simulation with NPCs whose traits and needs are fully set by the test.
int32 Spawn(FLisSimulation& Sim, ELisProfession Profession, ELisPlace Place = ELisPlace::Village)
{
    const int32 Id = Sim.AddNpc(Profession, Place);
    FLisNpc& Npc = *Sim.Find(Id);
    for (float& Trait : Npc.Personality.Value)
    {
        Trait = .5f;
    }
    for (float& Need : Npc.Needs)
    {
        Need = 0.f;
    }
    Npc.Skill[LisIdx(Profession)] = .45f;
    Npc.Goal = ELisGoal::MasterCraft;
    Npc.Food = 2.f;
    Npc.Coins = 6.f;
    Npc.Wood = 0.f;
    return Id;
}

FLisSimulation MakeDaySim()
{
    FLisSimulation Sim(77);
    Sim.Hour = 10;
    return Sim;
}

FLisOption Best(const FLisSimulation& Sim, int32 Id)
{
    const TArray<FLisOption> Options = Sim.Evaluate(*Sim.Find(Id));
    FLisOption Top = Options[0];
    for (const FLisOption& Option : Options)
    {
        if (Option.Score > Top.Score)
        {
            Top = Option;
        }
    }
    return Top;
}

float ScoreOf(const FLisSimulation& Sim, int32 Id, ELisAction Action)
{
    for (const FLisOption& Option : Sim.Evaluate(*Sim.Find(Id)))
    {
        if (Option.Action == Action)
        {
            return Option.Score;
        }
    }
    return -100.f;
}

// Starts an action as if the NPC had chosen it, so outcome code can be tested in isolation.
void Force(FLisSimulation& Sim, int32 Id, ELisAction Action, int32 Target = LisNobody)
{
    FLisNpc& Npc = *Sim.Find(Id);
    Npc.Intent = Npc.Current = Action;
    Npc.IntentPlace = Npc.Place;
    Npc.IntentTarget = Target;
    Npc.BusyUntil = Sim.Hour + 1;
}

FString LisSavedDir()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LIS"));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisDeterministicGeneration, "Aethra.LIS.01.DeterministicGeneration", LisTestFlags)
bool FLisDeterministicGeneration::RunTest(const FString&)
{
    FLisElden A(7), B(7), C(8);
    TestEqual(TEXT("Same seed, same village"), A.Sim.Checksum(), B.Sim.Checksum());
    TestNotEqual(TEXT("Different seed, different village"), A.Sim.Checksum(), C.Sim.Checksum());
    for (int32 I = 0; I < A.Sim.Npcs.Num(); ++I)
    {
        TestEqual(TEXT("Name is reproducible"), A.Sim.Npcs[I].Identity.Name, B.Sim.Npcs[I].Identity.Name);
    }
    int32 Identical = 0;
    for (int32 I = 0; I < A.Sim.Npcs.Num(); ++I)
    {
        for (int32 J = I + 1; J < A.Sim.Npcs.Num(); ++J)
        {
            Identical += FMemory::Memcmp(A.Sim.Npcs[I].Personality.Value, A.Sim.Npcs[J].Personality.Value, sizeof(FLisPersonality)) == 0;
        }
    }
    TestEqual(TEXT("No two generated personalities are identical"), Identical, 0);

    // Regression guard: the whole simulation is a pure function of the seed.
    A.Run(5 * 24);
    B.Run(5 * 24);
    TestEqual(TEXT("Five simulated days reproduce exactly"), A.Sim.Checksum(), B.Sim.Checksum());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisPersonalityChoices, "Aethra.LIS.02.PersonalityShapesChoices", LisTestFlags)
bool FLisPersonalityChoices::RunTest(const FString&)
{
    FLisSimulation Sim = MakeDaySim();
    const int32 Brave = Spawn(Sim, ELisProfession::Explorer);
    const int32 Careful = Spawn(Sim, ELisProfession::Explorer);
    for (const int32 Id : { Brave, Careful })
    {
        FLisNpc& Npc = *Sim.Find(Id);
        Npc.Need(ELisNeed::Work) = .6f;
        Npc.Need(ELisNeed::Purpose) = .6f;
        Npc.Beliefs[LisIdx(ELisPlace::Wilds)].Risk = .4f;
        Npc.Personality[ELisTrait::Curiosity] = .9f;
        const bool bBrave = Id == Brave;
        Npc.Personality[ELisTrait::Courage] = bBrave ? .9f : .1f;
        Npc.Personality[ELisTrait::Caution] = bBrave ? .1f : .9f;
    }
    TestEqual(TEXT("A brave explorer goes to the risky wilds"), Best(Sim, Brave).Action, ELisAction::Explore);
    TestNotEqual(TEXT("A cautious explorer avoids the risky wilds"), Best(Sim, Careful).Action, ELisAction::Explore);

    const int32 Kind = Spawn(Sim, ELisProfession::Merchant);
    const int32 Cold = Spawn(Sim, ELisProfession::Merchant);
    const int32 Hungry = Spawn(Sim, ELisProfession::Farmer);
    Sim.Find(Hungry)->Food = 0.f;
    Sim.Find(Hungry)->Need(ELisNeed::Food) = .9f;
    for (const int32 Id : { Kind, Cold })
    {
        FLisNpc& Npc = *Sim.Find(Id);
        Npc.Food = 5.f;
        Npc.Coins = 15.f;
        Npc.Need(ELisNeed::Purpose) = .3f;
        Npc.Need(ELisNeed::Work) = .3f;
        Npc.Personality[ELisTrait::Empathy] = Id == Kind ? .95f : .05f;
    }
    TestEqual(TEXT("An empathetic NPC feeds a hungry neighbour"), Best(Sim, Kind).Action, ELisAction::GiveHelp);
    TestEqual(TEXT("...and picks the hungry one"), Best(Sim, Kind).Target, Hungry);
    TestNotEqual(TEXT("A cold NPC with the same food does not"), Best(Sim, Cold).Action, ELisAction::GiveHelp);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisGoalEvaluation, "Aethra.LIS.03.UtilityEvaluation", LisTestFlags)
bool FLisGoalEvaluation::RunTest(const FString&)
{
    FLisSimulation Sim = MakeDaySim();
    const int32 Hungry = Spawn(Sim, ELisProfession::Farmer);
    Sim.Find(Hungry)->Need(ELisNeed::Food) = .9f;
    TestEqual(TEXT("Hungry with food in the pack: eat"), Best(Sim, Hungry).Action, ELisAction::Eat);
    const int32 Tired = Spawn(Sim, ELisProfession::Farmer);
    Sim.Find(Tired)->Need(ELisNeed::Rest) = .9f;
    TestEqual(TEXT("Exhausted: sleep"), Best(Sim, Tired).Action, ELisAction::Sleep);

    // The village runs short of food. Five villagers in the same plight weigh the same options.
    FLisSimulation Crisis = MakeDaySim();
    Crisis.World.Granary = 0.f;
    const int32 Friend = Spawn(Crisis, ELisProfession::Fisher);
    Crisis.Find(Friend)->Food = 6.f;
    Crisis.Find(Friend)->Personality[ELisTrait::Empathy] = .9f;
    struct FCase { ELisProfession Profession; TArray<TPair<ELisTrait, float>> Traits; ELisAction Expected; };
    const TArray<FCase> Cases = {
        { ELisProfession::Merchant, { { ELisTrait::Caution, .9f }, { ELisTrait::Ambition, .9f } }, ELisAction::BuyFood },
        { ELisProfession::Guard, { { ELisTrait::Sociability, .95f }, { ELisTrait::Ambition, .05f }, { ELisTrait::Caution, .1f } }, ELisAction::AskHelp },
        { ELisProfession::Farmer, { { ELisTrait::Loyalty, .9f }, { ELisTrait::Empathy, .9f } }, ELisAction::Farm },
        { ELisProfession::Explorer, { { ELisTrait::Curiosity, .95f }, { ELisTrait::Courage, .9f }, { ELisTrait::Caution, .1f } }, ELisAction::Explore },
        { ELisProfession::Fisher, {}, ELisAction::Fish },
    };
    TArray<int32> Ids;
    for (const FCase& Case : Cases)
    {
        const int32 Id = Spawn(Crisis, Case.Profession);
        FLisNpc& Npc = *Crisis.Find(Id);
        for (const TPair<ELisTrait, float>& Trait : Case.Traits)
        {
            Npc.Personality[Trait.Key] = Trait.Value;
        }
        Npc.Food = 0.f;
        Npc.Need(ELisNeed::Food) = .75f;
        Npc.Goal = ELisGoal::Survive;
        Npc.Beliefs[LisIdx(ELisPlace::Village)].Stock = 0.f;
        Npc.Beliefs[LisIdx(ELisPlace::Wilds)].bKnown = true;
        Npc.Beliefs[LisIdx(ELisPlace::Wilds)].Stock = 20.f;
        Ids.Add(Id);
    }
    // Only the sociable guard has a close friend to ask.
    FLisRelation& Bond = Crisis.Find(Ids[1])->Relation(Friend);
    Bond.Trust = .8f;
    Bond.Affection = .9f;

    TSet<ELisAction> Chosen;
    for (int32 I = 0; I < Cases.Num(); ++I)
    {
        const FLisOption Choice = Best(Crisis, Ids[I]);
        Chosen.Add(Choice.Action);
        AddInfo(FString::Printf(TEXT("%s chose %s (%.2f)"), LisName(Cases[I].Profession), LisName(Choice.Action), Choice.Score));
        TestEqual(FString::Printf(TEXT("Case %d follows its personality and means"), I), Choice.Action, Cases[I].Expected);
    }
    TestTrue(TEXT("The same crisis yields at least three strategies"), Chosen.Num() >= 3);
    const TArray<FLisOption> Options = Crisis.Evaluate(*Crisis.Find(Ids[1]));
    for (const ELisAction Action : { ELisAction::BuyFood, ELisAction::Farm, ELisAction::AskHelp, ELisAction::Explore })
    {
        TestTrue(FString::Printf(TEXT("%s is considered"), LisName(Action)), Options.ContainsByPredicate([Action](const FLisOption& O) { return O.Action == Action; }));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisPriorityShift, "Aethra.LIS.04.PriorityShift", LisTestFlags)
bool FLisPriorityShift::RunTest(const FString&)
{
    FLisSimulation Sim = MakeDaySim();
    const int32 Id = Spawn(Sim, ELisProfession::Farmer, ELisPlace::Fields);
    FLisNpc& Npc = *Sim.Find(Id);
    Npc.Need(ELisNeed::Work) = .8f;
    TestEqual(TEXT("Rested and fed: work"), Best(Sim, Id).Action, ELisAction::Farm);
    Npc.Need(ELisNeed::Food) = .95f;
    TestEqual(TEXT("Hunger overtakes work"), Sim.Decide(Npc).Action, ELisAction::Eat);
    Sim.Tick(1);
    TestTrue(TEXT("Eating lowered hunger"), Npc.Need(ELisNeed::Food) < .5f);
    TestEqual(TEXT("Fed again: back to work"), Npc.Intent, ELisAction::Farm);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisMemoryRecord, "Aethra.LIS.05.MemoryRecord", LisTestFlags)
bool FLisMemoryRecord::RunTest(const FString&)
{
    FLisSimulation Sim = MakeDaySim();
    const int32 Bold = Spawn(Sim, ELisProfession::Woodcutter, ELisPlace::Forest);
    const int32 Timid = Spawn(Sim, ELisProfession::Woodcutter, ELisPlace::Forest);
    const int32 Away = Spawn(Sim, ELisProfession::Farmer, ELisPlace::Village);
    Sim.Find(Bold)->Personality[ELisTrait::Courage] = .9f;
    Sim.Find(Bold)->Personality[ELisTrait::Caution] = .1f;
    Sim.Find(Timid)->Personality[ELisTrait::Courage] = .1f;
    Sim.Find(Timid)->Personality[ELisTrait::Caution] = .9f;

    const int32 Event = Sim.Emit(ELisEvent::Danger, ELisPlace::Forest, LisNobody, LisNobody, 1.f);
    const FLisMemory* Seen = Sim.Find(Bold)->FindMemory(Event);
    if (!TestNotNull(TEXT("A witness remembers"), Seen))
    {
        return false;
    }
    TestEqual(TEXT("...as observed"), Seen->Source, ELisSource::Observed);
    TestEqual(TEXT("...with full confidence"), Seen->Confidence, 1.f);
    TestEqual(TEXT("...at the right place and time"), Seen->Hour, Sim.Hour);
    TestTrue(TEXT("The same event is judged differently by a timid witness"), Sim.Find(Timid)->FindMemory(Event)->Valence < Seen->Valence);
    TestNull(TEXT("Someone elsewhere did not see it"), Sim.Find(Away)->FindMemory(Event));

    Sim.Find(Bold)->Place = ELisPlace::Village;
    TestTrue(TEXT("Conversation passes it on"), Sim.ShareKnowledge(*Sim.Find(Bold), *Sim.Find(Away)) > 0);
    const FLisMemory* Heard = Sim.Find(Away)->FindMemory(Event);
    if (!TestNotNull(TEXT("The listener now remembers it"), Heard))
    {
        return false;
    }
    TestEqual(TEXT("...as hearsay"), Heard->Source, ELisSource::Told);
    TestEqual(TEXT("...from the speaker"), Heard->ToldBy, Bold);
    TestTrue(TEXT("...with less confidence"), Heard->Confidence < 1.f);
    TestTrue(TEXT("Hearsay moves beliefs less than seeing"),
        Sim.Find(Away)->Beliefs[LisIdx(ELisPlace::Forest)].Risk < Sim.Find(Bold)->Beliefs[LisIdx(ELisPlace::Forest)].Risk);

    // Capacity: trivial memories are forgotten before important ones.
    Sim.Config.MemoryCapacity = 5;
    for (int32 I = 0; I < 10; ++I)
    {
        Sim.Emit(ELisEvent::TradeSucceeded, ELisPlace::Forest, LisNobody, LisNobody, 1.f);
    }
    TestTrue(TEXT("Memory stays within capacity"), Sim.Find(Timid)->Memories.Num() <= 5);
    TestNotNull(TEXT("The danger is still remembered"), Sim.Find(Timid)->FindMemory(Event));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisLimitedKnowledge, "Aethra.LIS.06.KnowledgeLimitedByPerception", LisTestFlags)
bool FLisLimitedKnowledge::RunTest(const FString&)
{
    FLisSimulation Sim = MakeDaySim();
    const int32 Scout = Spawn(Sim, ELisProfession::Explorer, ELisPlace::Wilds);
    const int32 Villager = Spawn(Sim, ELisProfession::Farmer, ELisPlace::Village);
    Sim.World.Place(ELisPlace::Wilds).Stock = 50.f;
    Sim.World.Place(ELisPlace::Wilds).Danger = 0.f;

    TestTrue(TEXT("The world changed but nobody knows yet"), Sim.Find(Villager)->Beliefs[LisIdx(ELisPlace::Wilds)].Stock < 5.f);
    Force(Sim, Scout, ELisAction::Explore);
    Sim.Tick(1);
    const FLisMemory* Found = Sim.Find(Scout)->Memories.FindByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::ResourceFound; });
    TestNotNull(TEXT("The explorer discovered the berries"), Found);
    TestTrue(TEXT("The explorer believes there is food in the wilds"), Sim.Find(Scout)->Beliefs[LisIdx(ELisPlace::Wilds)].Stock >= 5.f);
    TestTrue(TEXT("The villager far away still does not know"), Sim.Find(Villager)->Beliefs[LisIdx(ELisPlace::Wilds)].Stock < 5.f);
    TestFalse(TEXT("...and has no memory of it"), Sim.Find(Villager)->Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::ResourceFound; }));

    Sim.Find(Scout)->Place = ELisPlace::Village;
    Sim.ShareKnowledge(*Sim.Find(Scout), *Sim.Find(Villager));
    const FLisNpc& Told = *Sim.Find(Villager);
    TestTrue(TEXT("Once told, the villager knows of the wilds"), Told.Beliefs[LisIdx(ELisPlace::Wilds)].bKnown && Told.Beliefs[LisIdx(ELisPlace::Wilds)].Stock >= 5.f);
    TestTrue(TEXT("...from the explorer, not by magic"), Told.Memories.ContainsByPredicate([Scout](const FLisMemory& M) { return M.Type == ELisEvent::ResourceFound && M.ToldBy == Scout; }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisLearning, "Aethra.LIS.07.LearningFromExperience", LisTestFlags)
bool FLisLearning::RunTest(const FString&)
{
    FLisSimulation Sim = MakeDaySim();
    Sim.World.Place(ELisPlace::Fields).Danger = Sim.World.Place(ELisPlace::Fields).BaseDanger = 0.f;

    // Practice improves a profession.
    const int32 Farmer = Spawn(Sim, ELisProfession::Farmer, ELisPlace::Fields);
    const float SkillBefore = Sim.Find(Farmer)->Skill[LisIdx(ELisProfession::Farmer)];
    for (int32 I = 0; I < 10; ++I)
    {
        Force(Sim, Farmer, ELisAction::Farm);
        Sim.Tick(1);
    }
    const float SkillAfter = Sim.Find(Farmer)->Skill[LisIdx(ELisProfession::Farmer)];
    AddInfo(FString::Printf(TEXT("Farming skill %.3f -> %.3f after 10 harvests"), SkillBefore, SkillAfter));
    TestTrue(TEXT("Farming skill grows with practice"), SkillAfter > SkillBefore + .1f);

    // A path that proved dangerous becomes less attractive.
    const int32 Cutter = Spawn(Sim, ELisProfession::Woodcutter, ELisPlace::Village);
    Sim.Find(Cutter)->Need(ELisNeed::Work) = .7f;
    const float ChopBefore = ScoreOf(Sim, Cutter, ELisAction::Chop);
    Sim.Find(Cutter)->Place = ELisPlace::Forest;
    Sim.Emit(ELisEvent::Danger, ELisPlace::Forest, LisNobody, Cutter, 1.f);
    Sim.Emit(ELisEvent::Danger, ELisPlace::Forest, LisNobody, Cutter, 1.f);
    Sim.Find(Cutter)->Place = ELisPlace::Village;
    const float ChopAfter = ScoreOf(Sim, Cutter, ELisAction::Chop);
    AddInfo(FString::Printf(TEXT("Chop utility %.2f -> %.2f after two attacks in the forest"), ChopBefore, ChopAfter));
    TestTrue(TEXT("The woodcutter learned the forest is risky"), ChopAfter < ChopBefore - .2f);

    // A merchant who keeps losing negotiations values trading less.
    const int32 Trader = Spawn(Sim, ELisProfession::Merchant, ELisPlace::Market);
    Sim.Find(Trader)->Wood = 3.f;
    const float SellBefore = ScoreOf(Sim, Trader, ELisAction::Sell);
    Sim.World.TradeRisk = 1.f;
    for (int32 I = 0; I < 3; ++I)
    {
        Sim.Find(Trader)->Wood = 3.f;
        Force(Sim, Trader, ELisAction::Sell);
        Sim.Tick(1);
    }
    Sim.Find(Trader)->Wood = 3.f;
    const float SellAfter = ScoreOf(Sim, Trader, ELisAction::Sell);
    TestTrue(TEXT("Failed negotiations lower the expected value of selling"), Sim.Find(Trader)->Expectation[LisIdx(ELisAction::Sell)] < -.3f);
    TestTrue(TEXT("...and the merchant prefers it less"), SellAfter < SellBefore);

    // A guard who met a threat rates that place as more dangerous and is drawn to patrol it.
    const int32 Guard = Spawn(Sim, ELisProfession::Guard, ELisPlace::Forest);
    Sim.Find(Guard)->Personality[ELisTrait::Courage] = .9f;
    Sim.Find(Guard)->Personality[ELisTrait::Caution] = .2f;
    const float RiskBefore = Sim.Find(Guard)->Beliefs[LisIdx(ELisPlace::Forest)].Risk;
    const float PatrolBefore = ScoreOf(Sim, Guard, ELisAction::Patrol);
    Sim.Emit(ELisEvent::Danger, ELisPlace::Forest, LisNobody, Guard, 1.f);
    TestTrue(TEXT("The guard's danger estimate rose"), Sim.Find(Guard)->Beliefs[LisIdx(ELisPlace::Forest)].Risk > RiskBefore + .3f);
    TestTrue(TEXT("...and patrolling there matters more"), ScoreOf(Sim, Guard, ELisAction::Patrol) > PatrolBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisAsymmetricRelations, "Aethra.LIS.08.AsymmetricRelations", LisTestFlags)
bool FLisAsymmetricRelations::RunTest(const FString&)
{
    FLisSimulation Sim = MakeDaySim();
    const int32 Asker = Spawn(Sim, ELisProfession::Farmer);
    const int32 Giver = Spawn(Sim, ELisProfession::Fisher);
    const int32 Miser = Spawn(Sim, ELisProfession::Merchant);
    const int32 Witness = Spawn(Sim, ELisProfession::Guard);
    Sim.Find(Giver)->Personality[ELisTrait::Empathy] = .9f;
    Sim.Find(Giver)->Food = 5.f;
    Sim.Find(Miser)->Personality[ELisTrait::Empathy] = .05f;
    Sim.Find(Miser)->Personality[ELisTrait::Ambition] = .95f;
    Sim.Find(Miser)->Food = 5.f;
    Sim.Find(Asker)->Food = 0.f;
    // Keep the others in the village square for the whole test.
    for (const int32 Id : { Giver, Miser, Witness })
    {
        Force(Sim, Id, ELisAction::Socialize);
        Sim.Find(Id)->BusyUntil = Sim.Hour + 100;
    }

    Force(Sim, Asker, ELisAction::AskHelp, Giver);
    Sim.Tick(1);
    TestTrue(TEXT("The empathetic fisher shared food"), Sim.Find(Asker)->Food >= 1.f);
    const FLisRelation ToGiver = *Sim.Find(Asker)->FindRelation(Giver);
    const FLisRelation* FromGiver = Sim.Find(Giver)->FindRelation(Asker);
    TestTrue(TEXT("The helped trusts the helper more than the reverse"), ToGiver.Trust > (FromGiver ? FromGiver->Trust : .5f));
    TestTrue(TEXT("...and is fonder of them"), ToGiver.Affection > (FromGiver ? FromGiver->Affection : .5f));
    TestTrue(TEXT("A witness respects the helper"), Sim.Find(Witness)->FindRelation(Giver) && Sim.Find(Witness)->FindRelation(Giver)->Respect > .5f);

    Sim.Find(Asker)->Food = 0.f;
    Force(Sim, Asker, ELisAction::AskHelp, Miser);
    Sim.Tick(1);
    TestEqual(TEXT("The ambitious merchant refused"), Sim.Find(Asker)->Food, 0.f);
    TestTrue(TEXT("The refused one resents the merchant"), Sim.Find(Asker)->FindRelation(Miser)->Resentment > 0.f);
    const FLisRelation* FromMiser = Sim.Find(Miser)->FindRelation(Asker);
    TestTrue(TEXT("...but the merchant holds no grudge"), !FromMiser || FromMiser->Resentment == 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisPersistence, "Aethra.LIS.09.PersistenceAcrossRestart", LisTestFlags)
bool FLisPersistence::RunTest(const FString&)
{
    FLisElden Village(11);
    Village.Run(FLisElden::ShortageHour + 24);
    const FString Path = FPaths::Combine(LisSavedDir(), TEXT("LisTest_Persistence.lis"));
    IFileManager::Get().MakeDirectory(*LisSavedDir(), true);
    if (!TestTrue(TEXT("Saved to disk"), Village.Sim.SaveToFile(Path)))
    {
        return false;
    }

    FLisSimulation Restored;
    if (!TestTrue(TEXT("Loaded from disk"), Restored.LoadFromFile(Path)))
    {
        return false;
    }
    TestEqual(TEXT("Restored state is identical"), Restored.Checksum(), Village.Sim.Checksum());
    TestEqual(TEXT("Memories survive"), Restored.MemoryCount(), Village.Sim.MemoryCount());
    TestTrue(TEXT("The shortage is still remembered"), Restored.Npcs.ContainsByPredicate([](const FLisNpc& N)
    {
        return N.Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::FoodShortage; });
    }));

    // Consequences carry on: the restored world evolves exactly like the original.
    Village.Sim.Tick(48);
    Restored.Tick(48);
    TestEqual(TEXT("Two more days evolve identically after restore"), Restored.Checksum(), Village.Sim.Checksum());

    TArray<uint8> Corrupt = Village.Sim.SaveToBytes();
    Corrupt[4] = 99; // version field
    const uint32 Before = Restored.Checksum();
    TestFalse(TEXT("An unknown version is rejected"), Restored.LoadFromBytes(Corrupt));
    TestEqual(TEXT("...without touching the current state"), Restored.Checksum(), Before);
    Corrupt.SetNum(Corrupt.Num() / 2);
    Corrupt[4] = 1;
    TestFalse(TEXT("A truncated save is rejected"), Restored.LoadFromBytes(Corrupt));
    IFileManager::Get().Delete(*Path);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisElden20, "Aethra.LIS.10.EldenTwentyNpcs", LisTestFlags)
bool FLisElden20::RunTest(const FString&)
{
    FLisElden Elden(2026);
    TestEqual(TEXT("Twenty villagers"), Elden.Sim.Npcs.Num(), 20);

    Elden.Run(FLisElden::BerriesHour);
    const float Berries = Elden.Sim.World.Place(ELisPlace::Wilds).Stock;
    int32 Aware = 0;
    for (const FLisNpc& Npc : Elden.Sim.Npcs)
    {
        Aware += Npc.Beliefs[LisIdx(ELisPlace::Wilds)].Stock >= Berries;
    }
    TestEqual(TEXT("Nobody knows of the berries the hour they appear"), Aware, 0);

    Elden.Run(static_cast<int32>(FLisElden::LoggingHour - Elden.Sim.Hour));
    for (const FLisNpc& Npc : Elden.Sim.Npcs)
    {
        const bool bSaw = Npc.Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::ResourceRemoved && M.Source == ELisSource::Observed; });
        TestTrue(FString::Printf(TEXT("%s saw the logging only if in the forest"), *Npc.Identity.Name), !bSaw || Npc.Place == ELisPlace::Forest);
    }

    Elden.Run(static_cast<int32>(20 * 24 - Elden.Sim.Hour));
    TSet<ELisAction> Used;
    TSet<ELisAction> CrisisUsed;
    for (int32 I = 0; I < Elden.Sim.Npcs.Num(); ++I)
    {
        const FLisNpc& Npc = Elden.Sim.Npcs[I];
        TestTrue(FString::Printf(TEXT("%s made decisions"), *Npc.Identity.Name), Npc.Decisions > 0);
        for (int32 A = 0; A < LisNum<ELisAction>(); ++A)
        {
            if (Npc.ActionCount[A] > 0)
            {
                Used.Add(static_cast<ELisAction>(A));
            }
        }
        const TArray<int32> Crisis = Elden.CrisisActions(I);
        for (int32 A = 0; A < Crisis.Num(); ++A)
        {
            const ELisAction Action = static_cast<ELisAction>(A);
            if (Crisis[A] > 0 && Action != ELisAction::Travel && Action != ELisAction::Sleep && Action != ELisAction::Eat && Action != ELisAction::Idle)
            {
                CrisisUsed.Add(Action);
            }
        }
        for (const float Need : Npc.Needs)
        {
            TestTrue(TEXT("Needs stay in range"), Need >= 0.f && Need <= 1.f);
        }
        for (const TPair<int32, FLisRelation>& Pair : Npc.Relations)
        {
            TestTrue(TEXT("Relations stay in range"), Pair.Value.Trust >= 0.f && Pair.Value.Trust <= 1.f && Pair.Value.Resentment >= 0.f && Pair.Value.Resentment <= 1.f);
        }
    }
    TestTrue(TEXT("The village shows a varied set of activities"), Used.Num() >= 8);
    TestTrue(TEXT("The crisis produced at least four different responses"), CrisisUsed.Num() >= 4);

    const FLisStats& S = Elden.Sim.Stats;
    const double AvgMs = S.TotalTickMs / FMath::Max<int64>(1, S.Ticks);
    AddInfo(FString::Printf(TEXT("Elden: %lld hours, %lld decisions, %lld replans, %d memories, avg %.4f ms/hour, max %.4f ms, ~%.1f KB"),
        S.Ticks, S.Decisions, S.Replans, Elden.Sim.MemoryCount(), AvgMs, S.MaxTickMs, Elden.Sim.ApproxBytes() / 1024.0));
    TestTrue(TEXT("Twenty NPCs cost under 1 ms per simulated hour"), AvgMs < 1.0);

    const FString Report = Elden.Report();
    const FString Path = FPaths::Combine(LisSavedDir(), TEXT("Elden_seed2026.md"));
    FFileHelper::SaveStringToFile(Report, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    AddInfo(FString::Printf(TEXT("Report written to %s"), *FPaths::ConvertRelativePathToFull(Path)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVoxelAdapterTest, "Aethra.LIS.11.VoxelAdapter", LisTestFlags)
bool FLisVoxelAdapterTest::RunTest(const FString&)
{
    FLisSimulation Sim = MakeDaySim();
    const int32 Cutter = Spawn(Sim, ELisProfession::Woodcutter, ELisPlace::Forest);
    const int32 Farmer = Spawn(Sim, ELisProfession::Farmer, ELisPlace::Fields);
    const int32 Villager = Spawn(Sim, ELisProfession::Guard, ELisPlace::Village);

    FLisVoxelAdapter Adapter;
    TestEqual(TEXT("Without a classifier nothing is emitted"), (Adapter.OnBlockChanged({ FIntVector(96, 48, 40), 5, 0, LisPlayer }), Adapter.Flush(Sim)), 0);
    Adapter.Classify = [](uint8 Block) { return Block == 5 ? ELisVoxelResource::Wood : Block == 0 ? ELisVoxelResource::None : ELisVoxelResource::Soil; };

    const FIntVector Forest = Sim.World.Place(ELisPlace::Forest).Cell;
    const float StockBefore = Sim.World.Place(ELisPlace::Forest).Stock;
    for (int32 I = 0; I < 40; ++I)
    {
        Adapter.OnBlockChanged({ Forest + FIntVector(I % 8, I / 8, 40), 5, 0, LisPlayer });
    }
    Adapter.OnBlockChanged({ FIntVector(5000, 5000, 40), 5, 0, LisPlayer }); // far from any place
    TestEqual(TEXT("Changes are queued, not processed in the callback"), Adapter.PendingCount(), 41);
    TestEqual(TEXT("40 block changes become one LIS event"), Adapter.Flush(Sim), 1);
    TestEqual(TEXT("The change far from every place is ignored"), Adapter.Ignored, 2);
    TestEqual(TEXT("Forest stock drops by 40 blocks / 4 per unit"), Sim.World.Place(ELisPlace::Forest).Stock, StockBefore - 10.f);

    const FLisMemory* Seen = Sim.Find(Cutter)->Memories.FindByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::ResourceRemoved; });
    if (TestNotNull(TEXT("The woodcutter in the forest noticed"), Seen))
    {
        TestEqual(TEXT("...and knows a player did it"), Seen->Actor, LisPlayer);
        TestTrue(TEXT("...it matters to their work"), Seen->Valence < -.3f && Seen->Relevance >= .7f);
        TestTrue(TEXT("...and resents the player"), Sim.Find(Cutter)->FindRelation(LisPlayer) && Sim.Find(Cutter)->FindRelation(LisPlayer)->Resentment > 0.f);
    }
    TestTrue(TEXT("Farmer in the fields did not see it"), Sim.Find(Farmer)->Memories.IsEmpty());
    TestTrue(TEXT("Villager did not see it"), Sim.Find(Villager)->Memories.IsEmpty());

    // Inside the forest area but beyond the perception radius: counted, not perceived.
    const int32 Before = Sim.Find(Cutter)->Memories.Num();
    Adapter.OnBlockChanged({ Forest + FIntVector(55, 0, 40), 5, 0, LisPlayer });
    TestEqual(TEXT("A distant change still updates the world"), Adapter.Flush(Sim), 1);
    TestEqual(TEXT("...but nobody perceives it"), Sim.Find(Cutter)->Memories.Num(), Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisSchedulerTiers, "Aethra.LIS.12.SchedulerTiersAndBudget", LisTestFlags)
bool FLisSchedulerTiers::RunTest(const FString&)
{
    FLisSimulation Sim = MakeDaySim();
    Sim.Config.NearRadius = 2.f;
    Sim.Config.MidRadius = 4.f;
    const int32 Far = Spawn(Sim, ELisProfession::Explorer, ELisPlace::Wilds);
    const int32 Mid = Spawn(Sim, ELisProfession::Woodcutter, ELisPlace::Forest);
    TestEqual(TEXT("Wilds are far from the focus"), Sim.TierOf(ELisPlace::Wilds), ELisTier::Far);
    TestEqual(TEXT("Forest is mid distance"), Sim.TierOf(ELisPlace::Forest), ELisTier::Mid);
    TestEqual(TEXT("Village is near"), Sim.TierOf(ELisPlace::Village), ELisTier::Near);

    Sim.Find(Mid)->Need(ELisNeed::Work) = .8f;
    Sim.Tick(1);
    const FLisNpc& MidNpc = *Sim.Find(Mid);
    TestEqual(TEXT("Mid tier works in longer, batched steps"), MidNpc.BusyUntil - Sim.Hour, 6ll);

    Sim.Tick(23);
    TestEqual(TEXT("Far tier makes no individual decisions"), Sim.Find(Far)->Decisions, 0);
    TestTrue(TEXT("Far tier is simulated in aggregate"), Sim.Stats.AggregateSteps >= 2);

    FLisSimulation Busy = MakeDaySim();
    Busy.Config.MaxDecisionsPerTick = 3;
    for (int32 I = 0; I < 10; ++I)
    {
        Spawn(Busy, ELisProfession::Farmer);
    }
    Busy.Tick(1);
    TestEqual(TEXT("Budget caps decisions per tick"), Busy.Stats.Decisions, 3ll);
    TestEqual(TEXT("The rest wait for the next tick"), Busy.Stats.Deferred, 7ll);
    Busy.Tick(4);
    TestTrue(TEXT("Everyone gets a turn"), !Busy.Npcs.ContainsByPredicate([](const FLisNpc& N) { return N.Decisions == 0; }));
    return true;
}

#endif
