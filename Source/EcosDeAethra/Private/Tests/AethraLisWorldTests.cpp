#if WITH_DEV_AUTOMATION_TESTS

#include "AethraLisDirector.h"
#include "AethraLisNav.h"
#include "AethraLisNpc.h"
#include "AethraVoxelWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Voxel/AethraTerrain.h"

namespace
{
constexpr EAutomationTestFlags LisWorldFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr int32 LisWorldSeed = 2026; // not the game seed, so the demo save is never touched

// A game world with the voxel terrain and the LIS village, cleaned up afterwards
// (voxel save slots of the test seed and the LIS save file).
struct FLisWorldFixture
{
    UWorld* World = nullptr;
    AAethraVoxelWorld* Voxels = nullptr;
    AAethraLisDirector* Director = nullptr;
    FString SavePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LIS"), TEXT("Test_World.lis"));

    FLisWorldFixture()
    {
        DeleteSaves();
        World = UWorld::CreateWorld(EWorldType::Game, false);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
        Voxels = SpawnVoxels();
        Director = StartDirector();
    }

    ~FLisWorldFixture()
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        DeleteSaves();
    }

    AAethraVoxelWorld* SpawnVoxels() const
    {
        FActorSpawnParameters Params;
        Params.bDeferConstruction = true;
        AAethraVoxelWorld* Result = World->SpawnActor<AAethraVoxelWorld>(Params);
        Result->Seed = LisWorldSeed;
        Result->FinishSpawning(FTransform::Identity);
        return Result;
    }

    AAethraLisDirector* StartDirector() const
    {
        AAethraLisDirector* Result = World->SpawnActor<AAethraLisDirector>();
        Result->SecondsPerHour = 1.f; // one game hour per simulated second keeps the tests short
        Result->Start(Voxels, FIntPoint(6, 20), 20, SavePath);
        return Result;
    }

    void DeleteSaves() const
    {
        UGameplayStatics::DeleteGameInSlot(FString::Printf(TEXT("AethraWorld_%d_0"), LisWorldSeed), 0);
        UGameplayStatics::DeleteGameInSlot(FString::Printf(TEXT("AethraWorld_%d_1"), LisWorldSeed), 0);
        IFileManager::Get().Delete(*SavePath);
    }

    void Run(float Seconds, float Dt = 1.f / 30.f) const
    {
        for (float T = 0.f; T < Seconds; T += Dt)
        {
            Director->Step(Dt);
        }
    }

    FIntPoint Column(const AAethraLisNpc& Body) const
    {
        const FIntVector Cell = Body.CurrentCell();
        return FIntPoint(Cell.X, Cell.Y);
    }

    // The ground block under a walkable column next to the body (something real to dig).
    FIntVector GroundNear(const AAethraLisNpc& Body, int32 Dx) const
    {
        FIntPoint P = Column(Body) + FIntPoint(Dx, 0);
        Director->Nav().NearestWalkable(P, 2, P);
        const FIntVector Stand = Director->Nav().Stand(P);
        return FIntVector(Stand.X, Stand.Y, Stand.Z - 1);
    }

    void Wall(FIntPoint P, int32 BaseZ) const
    {
        for (int32 Z = BaseZ; Z < BaseZ + 3; ++Z)
        {
            Voxels->SetBlock(FIntVector(P.X, P.Y, Z), EAethraBlock::Stone);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldInit, "Aethra.LISWorld.01.Initialization", LisWorldFlags)
bool FLisWorldInit::RunTest(const FString&)
{
    FLisWorldFixture F;
    if (!TestTrue(TEXT("The LIS starts inside the game world"), F.Director->IsRunning()))
    {
        return false;
    }
    TestTrue(TEXT("Travel is driven by the 3D world"), F.Director->Sim().Config.bExternalTravel);
    const FIntVector Village = F.Director->Anchor(ELisPlace::Village);
    for (int32 P = 0; P < LisNum<ELisPlace>(); ++P)
    {
        const FIntVector A = F.Director->Anchor(static_cast<ELisPlace>(P));
        TestTrue(FString::Printf(TEXT("%s is on walkable ground"), LisName(static_cast<ELisPlace>(P))), F.Director->Nav().Walkable(FIntPoint(A.X, A.Y)));
        TArray<FIntVector> Path;
        TestTrue(FString::Printf(TEXT("%s can be reached from the village"), LisName(static_cast<ELisPlace>(P))),
            F.Director->Nav().FindPath(FIntPoint(Village.X, Village.Y), FIntPoint(A.X, A.Y), Path));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldBodies, "Aethra.LISWorld.02.NpcCreation", LisWorldFlags)
bool FLisWorldBodies::RunTest(const FString&)
{
    FLisWorldFixture F;
    TestEqual(TEXT("Twenty NPC bodies"), F.Director->Npcs().Num(), 20);
    for (const AAethraLisNpc* Body : F.Director->Npcs())
    {
        if (TestNotNull(TEXT("Body spawned"), Body))
        {
            TestTrue(TEXT("...standing on walkable ground"), F.Director->Nav().Walkable(F.Column(*Body)));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldIdentity, "Aethra.LISWorld.03.IdentityLink", LisWorldFlags)
bool FLisWorldIdentity::RunTest(const FString&)
{
    FLisWorldFixture F;
    TSet<FString> Names;
    for (const FLisNpc& Npc : F.Director->Sim().Npcs)
    {
        const AAethraLisNpc* Body = F.Director->BodyOf(Npc.Identity.Id);
        TestTrue(FString::Printf(TEXT("%s has exactly its own body"), *Npc.Identity.Name), Body && Body->NpcId == Npc.Identity.Id);
        Names.Add(Npc.Identity.Name);
    }
    TestEqual(TEXT("Every villager has a distinct name"), Names.Num(), F.Director->Sim().Npcs.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldGoals, "Aethra.LISWorld.04.AutonomousGoals", LisWorldFlags)
bool FLisWorldGoals::RunTest(const FString&)
{
    FLisWorldFixture F;
    F.Run(3.f);
    const FLisSimulation& S = F.Director->Sim();
    TSet<ELisAction> Chosen;
    for (const FLisNpc& Npc : S.Npcs)
    {
        TestTrue(FString::Printf(TEXT("%s decided by itself"), *Npc.Identity.Name), Npc.Decisions > 0);
        Chosen.Add(Npc.Current == ELisAction::Travel ? Npc.Intent : Npc.Current);
        if (Npc.Current == ELisAction::Travel)
        {
            // The body walks where the LIS decided to go.
            const AAethraLisNpc* Body = F.Director->BodyOf(Npc.Identity.Id);
            const FIntVector Target = F.Director->Anchor(Npc.IntentPlace);
            TestTrue(FString::Printf(TEXT("%s walks towards %s"), *Npc.Identity.Name, LisName(Npc.IntentPlace)),
                Body && FVector2D(Body->Goal.X - Target.X, Body->Goal.Y - Target.Y).Size() <= 8.0);
        }
    }
    AddInfo(FString::Printf(TEXT("%d different activities after 3 game hours"), Chosen.Num()));
    TestTrue(TEXT("Villagers chose different activities"), Chosen.Num() >= 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldMovement, "Aethra.LISWorld.05.Movement", LisWorldFlags)
bool FLisWorldMovement::RunTest(const FString&)
{
    FLisWorldFixture F;
    F.Run(8.f);
    int32 Arrivals = 0;
    for (const FLisNpc& Npc : F.Director->Sim().Npcs)
    {
        Arrivals += Npc.Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::Arrived; });
        const AAethraLisNpc* Body = F.Director->BodyOf(Npc.Identity.Id);
        if (Body && Npc.Current != ELisAction::Travel && Body->NavState != EAethraNavState::Walking && Body->NavState != EAethraNavState::Waiting)
        {
            const FIntVector A = F.Director->Anchor(Npc.Place);
            const double Gap = FVector2D(Body->CurrentCell().X - A.X, Body->CurrentCell().Y - A.Y).Size();
            TestTrue(FString::Printf(TEXT("%s stands at %s (%.1f cells from it)"), *Npc.Identity.Name, LisName(Npc.Place), Gap), Gap <= 16.0);
        }
    }
    AddInfo(FString::Printf(TEXT("%d villagers walked to a destination and arrived"), Arrivals));
    TestTrue(TEXT("Villagers walked to their destinations and arrived"), Arrivals >= 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldReplan, "Aethra.LISWorld.06.PathReplanning", LisWorldFlags)
bool FLisWorldReplan::RunTest(const FString&)
{
    FLisWorldFixture F;
    FLisSimulation& S = F.Director->Sim();
    FLisNpc& Npc = S.Npcs[0];
    AAethraLisNpc* Body = F.Director->BodyOf(Npc.Identity.Id);
    // Send the NPC to the farthest place, as if the LIS had chosen it.
    Npc.Current = ELisAction::Travel;
    Npc.Intent = ELisAction::Explore;
    Npc.IntentPlace = ELisPlace::Wilds;
    Npc.BusyUntil = MAX_int64;
    F.Run(.2f);
    if (!TestEqual(TEXT("The NPC is walking"), Body->NavState, EAethraNavState::Walking) || !TestTrue(TEXT("...on a long path"), Body->Path.Num() > Body->PathIndex + 8))
    {
        return false;
    }
    // A wall appears across the path: only this NPC's route is planned again.
    const FIntVector Ahead = Body->Path[Body->PathIndex + 5];
    const int32 ReplansBefore = Body->PathReplans;
    F.Wall(FIntPoint(Ahead.X, Ahead.Y), Ahead.Z);
    F.Run(.2f);
    TestTrue(TEXT("The blocked path was planned again"), Body->PathReplans > ReplansBefore);
    TestEqual(TEXT("...and the NPC keeps walking"), Body->NavState, EAethraNavState::Walking);
    TestFalse(TEXT("...around the wall"), Body->PathUses(FIntPoint(Ahead.X, Ahead.Y)));

    // The destination becomes unreachable: the world tells the LIS, which replans.
    const FIntVector Goal = Body->Goal;
    const int32 LisReplans = Npc.Replans;
    F.Wall(FIntPoint(Goal.X, Goal.Y), Goal.Z);
    F.Run(.5f);
    TestTrue(TEXT("The LIS learned the trip failed"), Npc.Replans > LisReplans);
    TestFalse(TEXT("...and is no longer travelling there"), Npc.IntentPlace == ELisPlace::Wilds && Npc.Current == ELisAction::Travel);
    TestFalse(TEXT("The failure is visible in the debug data"), Body->LastError.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldPerception, "Aethra.LISWorld.07.Perception", LisWorldFlags)
bool FLisWorldPerception::RunTest(const FString&)
{
    FLisWorldFixture F;
    AAethraLisNpc* Near = F.Director->BodyOf(1);
    AAethraLisNpc* Far = F.Director->BodyOf(2);
    // One villager is sent 45 cells away; the other stays next to the change.
    Far->SetActorLocation(Near->GetActorLocation() + FVector(4500, 0, 0));
    const FIntVector Ground = F.GroundNear(*Near, 2);
    const EAethraBlock Before = F.Voxels->GetBlock(Ground);
    TestTrue(TEXT("There is ground to dig"), Before != EAethraBlock::Air);
    TestTrue(TEXT("The player removes a block"), F.Voxels->SetBlockBy(Ground, EAethraBlock::Air, FAethraInstigator::Player()));
    F.Director->Step(1.f / 30.f);

    const FLisNpc& Witness = *F.Director->Sim().Find(1);
    const FLisMemory* Seen = Witness.Memories.FindByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::ResourceRemoved; });
    if (TestNotNull(TEXT("The villager next to it noticed"), Seen))
    {
        TestEqual(TEXT("...and knows the player did it"), Seen->Actor, LisPlayer);
    }
    TestFalse(TEXT("The villager far away did not"), F.Director->Sim().Find(2)->Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::ResourceRemoved; }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldMemory, "Aethra.LISWorld.08.MemoryRecord", LisWorldFlags)
bool FLisWorldMemory::RunTest(const FString&)
{
    FLisWorldFixture F;
    AAethraLisNpc* Near = F.Director->BodyOf(3);
    const FIntVector Ground = F.GroundNear(*Near, -2);
    F.Voxels->SetBlockBy(Ground, EAethraBlock::Air, FAethraInstigator::Player());
    F.Director->Step(1.f / 30.f);
    const FLisSimulation& S = F.Director->Sim();
    const FLisMemory* M = S.Find(3)->Memories.FindByPredicate([](const FLisMemory& Mem) { return Mem.Type == ELisEvent::ResourceRemoved; });
    if (!TestNotNull(TEXT("The change was remembered"), M))
    {
        return false;
    }
    TestEqual(TEXT("What: a resource was removed"), M->Type, ELisEvent::ResourceRemoved);
    TestEqual(TEXT("When: this game hour"), M->Hour, S.Hour);
    TestEqual(TEXT("How: seen with its own eyes"), M->Source, ELisSource::Observed);
    TestEqual(TEXT("Who: the player"), M->Actor, LisPlayer);
    TestTrue(TEXT("Relevance was judged"), M->Relevance > 0.f);
    const FLisEventRecord* Fact = S.Events.FindByPredicate([M](const FLisEventRecord& E) { return E.Id == M->EventId; });
    TestTrue(TEXT("Where: the exact cell is in the event log"), Fact && Fact->Cell == Ground);

    F.Run(6.f);
    const bool bArrival = S.Npcs.ContainsByPredicate([](const FLisNpc& Npc)
    {
        return Npc.Memories.ContainsByPredicate([&Npc](const FLisMemory& Mem) { return Mem.Type == ELisEvent::Arrived && Mem.Actor == Npc.Identity.Id && Mem.Source == ELisSource::Observed; });
    });
    TestTrue(TEXT("Arrivals at a place are remembered by the traveller"), bArrival);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldSocial, "Aethra.LISWorld.09.Socialization", LisWorldFlags)
bool FLisWorldSocial::RunTest(const FString&)
{
    FLisWorldFixture F;
    FLisSimulation& S = F.Director->Sim();
    F.Director->SecondsPerHour = 1000.f; // no LIS hour passes: only the encounter matters here
    F.Director->Step(1.f / 30.f);
    AAethraLisNpc* A = F.Director->BodyOf(4);
    AAethraLisNpc* B = F.Director->BodyOf(5);
    // A walks slowly past B, away from the crowd (each villager notes one encounter per hour).
    A->Stop();
    B->Stop();
    A->SetActorLocation(A->GetActorLocation() + FVector(3000, 3000, 0));
    B->SetActorLocation(A->GetActorLocation() + FVector(150, 0, 0));
    TArray<FIntVector> Path = { A->CurrentCell() + FIntVector(1, 0, 0), A->CurrentCell() + FIntVector(2, 0, 0) };
    A->FollowPath(Path, Path.Last());
    A->WalkSpeed = 40.f;
    F.Run(1.1f);
    TestTrue(TEXT("A remembers meeting B"), S.Find(4)->Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::Met && M.Target == 5; }));
    TestTrue(TEXT("B remembers meeting A"), S.Find(5)->Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::Met && M.Actor == 4; }));
    TestTrue(TEXT("The meeting touched their relation"), S.Find(4)->FindRelation(5) != nullptr);

    // Socialising: the LIS chooses a partner and the body walks up to them.
    FLisNpc& Talker = *S.Find(6);
    AAethraLisNpc* TalkerBody = F.Director->BodyOf(6);
    B->SetActorLocation(TalkerBody->GetActorLocation() + FVector(900, 0, 0));
    B->Stop();
    Talker.Current = ELisAction::Socialize;
    Talker.IntentTarget = 5;
    Talker.BusyUntil = S.Hour + 2;
    F.Run(.3f);
    const FIntVector Partner = B->CurrentCell();
    TestTrue(TEXT("The talker heads to the person the LIS chose"),
        FVector2D(TalkerBody->Goal.X - Partner.X, TalkerBody->Goal.Y - Partner.Y).Size() <= 3.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldPersistence, "Aethra.LISWorld.10.PersistenceAfterRestart", LisWorldFlags)
bool FLisWorldPersistence::RunTest(const FString&)
{
    FLisWorldFixture F;
    F.Run(5.f);
    const uint32 Checksum = F.Director->Sim().Checksum();
    const int32 Memories = F.Director->Sim().MemoryCount();
    const FIntVector Forest = F.Director->Anchor(ELisPlace::Forest);
    TestTrue(TEXT("Saved"), F.Director->Save());

    // A new director (a new session) restores the village from disk instead of creating it again.
    F.Director->Destroy();
    AAethraLisDirector* Restored = F.StartDirector();
    TestEqual(TEXT("Identical LIS state after restart"), Restored->Sim().Checksum(), Checksum);
    TestEqual(TEXT("Memories survive"), Restored->Sim().MemoryCount(), Memories);
    TestEqual(TEXT("Places keep their real coordinates"), Restored->Anchor(ELisPlace::Forest), Forest);
    TestEqual(TEXT("Bodies are spawned for every villager"), Restored->Npcs().Num(), 20);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldVoxelSave, "Aethra.LISWorld.11.VoxelSaveCompatibility", LisWorldFlags)
bool FLisWorldVoxelSave::RunTest(const FString&)
{
    FLisWorldFixture F;
    FLisSimulation& S = F.Director->Sim();
    // A woodcutter next to a real trunk near the forest.
    const FIntVector Forest = F.Director->Anchor(ELisPlace::Forest);
    FIntVector Trunk(0), Stand(0);
    bool bFound = false;
    for (int32 Y = -12; Y <= 12 && !bFound; ++Y)
    {
        for (int32 X = -12; X <= 12 && !bFound; ++X)
        {
            const FIntPoint T(Forest.X + X, Forest.Y + Y);
            const int16 Base = F.Director->Nav().Column(T).TrunkBaseZ;
            FIntPoint Next;
            if (Base >= 0 && F.Director->Nav().NearestWalkable(T, 1, Next))
            {
                Trunk = FIntVector(T.X, T.Y, Base);
                Stand = F.Director->Nav().Stand(Next);
                bFound = true;
            }
        }
    }
    if (!TestTrue(TEXT("A tree stands near the forest anchor"), bFound))
    {
        return false;
    }
    const int32 Cutter = S.Npcs.IndexOfByPredicate([](const FLisNpc& N) { return N.Identity.Profession == ELisProfession::Woodcutter; }) + 1;
    AAethraLisNpc* Body = F.Director->BodyOf(Cutter);
    Body->SetActorLocation(FVector((Stand.X + .5) * 100.0, (Stand.Y + .5) * 100.0, Stand.Z * 100.0 + 90.0));
    const EAethraBlock Wood = F.Voxels->GetBlock(Trunk);

    TestEqual(TEXT("Wrong expectation is refused"), F.Director->ApplyBlockRequest({ Cutter, Trunk, EAethraBlock::Stone, EAethraBlock::Air }), EAethraBlockVerdict::WrongBlock);
    const FIntVector Ground(Stand.X, Stand.Y, Stand.Z - 1);
    TestEqual(TEXT("Digging the ground is not allowed"), F.Director->ApplyBlockRequest({ Cutter, Ground, F.Voxels->GetBlock(Ground), EAethraBlock::Air }), EAethraBlockVerdict::NotAllowed);
    TestEqual(TEXT("Felling the trunk next to it is applied"), F.Director->ApplyBlockRequest({ Cutter, Trunk, Wood, EAethraBlock::Air }), EAethraBlockVerdict::Applied);
    const FIntVector Above = Trunk + FIntVector(0, 0, 1);
    TestEqual(TEXT("One change per game hour"), F.Director->ApplyBlockRequest({ Cutter, Above, F.Voxels->GetBlock(Above), EAethraBlock::Air }), EAethraBlockVerdict::RateLimited);
    Body->SetActorLocation(Body->GetActorLocation() + FVector(2000, 0, 0));
    S.Tick(1);
    TestEqual(TEXT("Out of reach is refused"), F.Director->ApplyBlockRequest({ Cutter, Above, F.Voxels->GetBlock(Above), EAethraBlock::Air }), EAethraBlockVerdict::OutOfReach);
    F.Director->Step(1.f / 30.f);
    TestTrue(TEXT("The NPC change reached the LIS with the NPC as author"), S.Events.ContainsByPredicate([Cutter](const FLisEventRecord& E)
    {
        return E.Type == ELisEvent::ResourceRemoved && E.Actor == Cutter;
    }));

    // The voxel save written by the NPC edit is an ordinary voxel save: a fresh voxel world restores it.
    AAethraVoxelWorld* Again = F.SpawnVoxels();
    Again->PrepareSpawn();
    TestEqual(TEXT("The felled block is restored from the voxel save"), Again->GetBlock(Trunk), EAethraBlock::Air);
    TestTrue(TEXT("The LIS writes its own file, not a voxel slot"), F.Director->SaveFile.EndsWith(TEXT(".lis")) && F.Director->Save()
        && UGameplayStatics::DoesSaveGameExist(FString::Printf(TEXT("AethraWorld_%d_1"), LisWorldSeed), 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisWorldPerformance, "Aethra.LISWorld.12.Performance20Npcs", LisWorldFlags)
bool FLisWorldPerformance::RunTest(const FString&)
{
    FLisWorldFixture F;
    F.Director->SecondsPerHour = 2.f;
    F.Run(30.f, 1.f / 60.f); // 1800 frames, 15 game hours
    const FAethraLisFrameStats& A = F.Director->Average;
    AddInfo(FString::Printf(TEXT("20 NPCs, %lld frames: director avg %.4f ms (LIS %.4f, perception %.4f, paths %.4f, movement %.4f), max %.3f ms, %d paths (%d failed), %d columns read"),
        F.Director->Frames, A.TotalMs, A.LisMs, A.PerceptionMs, A.NavMs, A.MoveMs, F.Director->MaxTotalMs,
        F.Director->Nav().PathsSearched, F.Director->Nav().PathsFailed, F.Director->Nav().ColumnsComputed));
    TestTrue(TEXT("Villagers kept deciding"), F.Director->Sim().Stats.Decisions >= 40);
    // A generous budget (a quarter of a 60 fps frame) only catches pathological regressions.
    TestTrue(TEXT("Average director cost stays under 4 ms per frame"), A.TotalMs < 4.0);
    return true;
}

#endif
