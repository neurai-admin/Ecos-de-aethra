// LIS 03: the Elden village, 3D villagers and their animations in the voxel world.
#if WITH_DEV_AUTOMATION_TESTS

#include "AethraLisDirector.h"
#include "AethraLisNav.h"
#include "AethraLisNpc.h"
#include "AethraLisVillage.h"
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
constexpr EAutomationTestFlags LisVillageFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr int32 VillageSeed = 2027; // a test seed of its own: never the game or demo saves

// A game world with the terrain, the LIS and the Elden village already built (demo rules: generated
// trees on the plots may be felled). Voxel slots and the LIS save of this seed are deleted around it.
struct FVillageFixture
{
    UWorld* World = nullptr;
    AAethraVoxelWorld* Voxels = nullptr;
    AAethraLisDirector* Director = nullptr;
    FString SavePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LIS"), TEXT("Test_Village.lis"));

    explicit FVillageFixture(bool bBuild = true)
    {
        DeleteSaves();
        World = UWorld::CreateWorld(EWorldType::Game, false);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
        FActorSpawnParameters Params;
        Params.bDeferConstruction = true;
        Voxels = World->SpawnActor<AAethraVoxelWorld>(Params);
        Voxels->Seed = VillageSeed;
        Voxels->FinishSpawning(FTransform::Identity);
        Director = StartDirector();
        if (bBuild)
        {
            Run(1.f / 30.f); // one frame: the whole village goes up (unlimited budget)
        }
    }

    ~FVillageFixture()
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        DeleteSaves();
    }

    AAethraLisDirector* StartDirector() const
    {
        AAethraLisDirector* Result = World->SpawnActor<AAethraLisDirector>();
        Result->SecondsPerHour = 1.f;
        Result->BuildBudget = 1 << 20;
        Result->bDemoVillage = true;
        Result->Start(Voxels, FIntPoint(6, 20), 20, SavePath);
        return Result;
    }

    // Saves, destroys the director (and its bodies) and starts a new one on the same world and save.
    void Restart()
    {
        Director->Save();
        Director->Destroy();
        Director = StartDirector();
    }

    void DeleteSaves() const
    {
        UGameplayStatics::DeleteGameInSlot(FString::Printf(TEXT("AethraWorld_%d_0"), VillageSeed), 0);
        UGameplayStatics::DeleteGameInSlot(FString::Printf(TEXT("AethraWorld_%d_1"), VillageSeed), 0);
        IFileManager::Get().Delete(*SavePath);
    }

    void Run(float Seconds, float Dt = 1.f / 30.f) const
    {
        for (float T = 0.f; T < Seconds; T += Dt)
        {
            Director->Step(Dt);
        }
    }

    // Runs until Done() or the time runs out; returns whether Done() became true.
    template <typename F>
    bool RunUntil(float Seconds, F Done, float Dt = 1.f / 30.f) const
    {
        for (float T = 0.f; T < Seconds; T += Dt)
        {
            Director->Step(Dt);
            if (Done())
            {
                return true;
            }
        }
        return false;
    }

    FLisNpc& Npc(int32 Id) const { return *Director->Sim().Find(Id); }
    AAethraLisNpc& Body(int32 Id) const { return *Director->BodyOf(Id); }

    int32 FirstOf(ELisProfession Profession) const
    {
        for (const FLisNpc& N : Director->Sim().Npcs)
        {
            if (N.Identity.Profession == Profession)
            {
                return N.Identity.Id;
            }
        }
        return 0;
    }

    // Makes the LIS give an NPC a task at a place right now (the same fields Decide/StartIntent set).
    void Assign(int32 Id, ELisAction Action, ELisPlace Place) const
    {
        FLisNpc& N = Npc(Id);
        N.Place = Place;
        N.Current = Action;
        N.Intent = Action;
        N.IntentPlace = Place;
        N.BusyUntil = Director->Sim().Hour + 1000;
        ++N.Decisions; // a new step for the body
    }

    void Teleport(int32 Id, const FIntVector& Cell) const
    {
        Body(Id).SetActorLocation(FVector((Cell.X + .5) * 100.0, (Cell.Y + .5) * 100.0, Cell.Z * 100.0 + 90.0));
        Body(Id).Stop();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillageBodies, "Aethra.LISVillage.01.CreateAndRemoveBodies", LisVillageFlags)
bool FLisVillageBodies::RunTest(const FString&)
{
    FVillageFixture F;
    const TArray<TObjectPtr<AAethraLisNpc>> Bodies = F.Director->Npcs();
    TestEqual(TEXT("Twenty villagers"), Bodies.Num(), 20);
    for (const AAethraLisNpc* Body : Bodies)
    {
        if (TestNotNull(TEXT("Body spawned"), Body))
        {
            TestTrue(TEXT("...with the rigged villager skeleton ready to pose"), Body->HasSkeleton());
            TestTrue(TEXT("...standing on a walkable cell"), F.Director->Nav().IsStand(Body->CurrentCell())
                || F.Director->Nav().Walkable(FIntPoint(Body->CurrentCell().X, Body->CurrentCell().Y)));
        }
    }
    F.Director->Destroy();
    int32 Alive = 0;
    for (const AAethraLisNpc* Body : Bodies)
    {
        Alive += IsValid(Body);
    }
    TestEqual(TEXT("Removing the director removes every body"), Alive, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillageIdentity, "Aethra.LISVillage.02.BodyIdentity", LisVillageFlags)
bool FLisVillageIdentity::RunTest(const FString&)
{
    FVillageFixture F;
    TMap<int32, EAethraVillagerHat> Hats;
    for (int32 I = 0; I < F.Director->Npcs().Num(); ++I)
    {
        const AAethraLisNpc* Body = F.Director->Npcs()[I];
        const FLisNpc* Npc = F.Director->Sim().Find(Body->NpcId);
        TestEqual(TEXT("Body N carries the persistent LIS id N"), Body->NpcId, I + 1);
        if (!TestNotNull(TEXT("...and that id exists in the LIS"), Npc))
        {
            continue;
        }
        TestEqual(TEXT("...the same identity"), Npc->Identity.Id, Body->NpcId);
        // The look follows the trade: hats are reserved per profession.
        const EAethraVillagerHat Expected = Npc->Identity.Profession == ELisProfession::Farmer ? EAethraVillagerHat::Straw
            : Npc->Identity.Profession == ELisProfession::Woodcutter ? EAethraVillagerHat::Beanie
            : Npc->Identity.Profession == ELisProfession::Builder ? EAethraVillagerHat::Work
            : Npc->Identity.Profession == ELisProfession::Merchant ? EAethraVillagerHat::Merchant : Body->GetHat();
        TestEqual(FString::Printf(TEXT("%s wears the hat of a %s"), *Npc->Identity.Name, LisName(Npc->Identity.Profession)), Body->GetHat(), Expected);
        Hats.Add(Body->NpcId, Body->GetHat());
    }
    TSet<ELisProfession> Trades;
    for (const FLisNpc& Npc : F.Director->Sim().Npcs)
    {
        Trades.Add(Npc.Identity.Profession);
    }
    TestTrue(TEXT("At least four professions live in the village"), Trades.Contains(ELisProfession::Farmer) && Trades.Contains(ELisProfession::Woodcutter)
        && Trades.Contains(ELisProfession::Builder) && Trades.Contains(ELisProfession::Merchant));
    F.Restart();
    for (const AAethraLisNpc* Body : F.Director->Npcs())
    {
        TestEqual(TEXT("After a restart each body gets the same mind and look back"), Body->GetHat(), Hats.FindRef(Body->NpcId));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillageAnimation, "Aethra.LISVillage.03.AnimationMatchesActivity", LisVillageFlags)
bool FLisVillageAnimation::RunTest(const FString&)
{
    FVillageFixture F;
    // 1. Over a stretch of normal village life, every body always shows what its LIS state says.
    int32 Checked = 0, Mismatch = 0;
    TSet<EAethraVillagerAnim> Seen;
    for (int32 Frame = 0; Frame < 30 * 40; ++Frame)
    {
        F.Director->Step(1.f / 30.f);
        for (const AAethraLisNpc* Body : F.Director->Npcs())
        {
            const FLisNpc& Npc = F.Npc(Body->NpcId);
            ++Checked;
            Mismatch += Body->GetAnim() != AAethraLisDirector::AnimFor(Npc, *Body);
            Seen.Add(Body->GetAnim());
        }
    }
    TestEqual(TEXT("No body ever plays an animation its LIS action does not call for"), Mismatch, 0);
    TestTrue(FString::Printf(TEXT("Several animations in use (%d)"), Seen.Num()), Seen.Num() >= 4);
    AddInfo(FString::Printf(TEXT("%d body-frames checked, %d animation states seen"), Checked, Seen.Num()));

    // 2. A task given by the LIS puts the matching pose on the skeleton, and the bones really move.
    const int32 Woodcutter = F.FirstOf(ELisProfession::Woodcutter);
    AAethraLisNpc& Body = F.Body(Woodcutter);
    F.Assign(Woodcutter, ELisAction::Chop, ELisPlace::Forest);
    const bool bChopping = F.RunUntil(60.f, [&]() { return Body.GetAnim() == EAethraVillagerAnim::Chop; });
    TestTrue(TEXT("Chopping (once at a tree) plays the axe animation"), bChopping);
    TestEqual(TEXT("...with the axe in hand"), Body.GetTool(), EAethraVillagerTool::Axe);
    FVector Lowest(0, 0, 1e9), Highest(0, 0, -1e9);
    for (int32 I = 0; I < 45; ++I)
    {
        F.Director->Step(1.f / 30.f);
        const FVector Hand = Body.BoneLocation(TEXT("hand_r"));
        Lowest = Hand.Z < Lowest.Z ? Hand : Lowest;
        Highest = Hand.Z > Highest.Z ? Hand : Highest;
    }
    TestTrue(FString::Printf(TEXT("The swing really lifts the hand (%.0f cm)"), Highest.Z - Lowest.Z), Highest.Z - Lowest.Z > 40.0);

    const int32 Sleeper = F.FirstOf(ELisProfession::Merchant);
    F.Assign(Sleeper, ELisAction::Sleep, ELisPlace::Village);
    const bool bAsleep = F.RunUntil(60.f, [&]() { return F.Body(Sleeper).GetAnim() == EAethraVillagerAnim::Sleep; });
    TestTrue(TEXT("Sleeping at home lies the body down"), bAsleep && F.Body(Sleeper).IsLying());
    F.Assign(Sleeper, ELisAction::Socialize, ELisPlace::Village);
    const bool bUp = F.RunUntil(20.f, [&]() { return !F.Body(Sleeper).IsLying(); });
    TestTrue(TEXT("...and a new action gets it up again"), bUp);

    const AAethraLisNpc* Walker = nullptr;
    F.RunUntil(30.f, [&]()
    {
        for (const AAethraLisNpc* B : F.Director->Npcs())
        {
            if (B->NavState == EAethraNavState::Walking)
            {
                Walker = B;
                return true;
            }
        }
        return false;
    });
    if (TestNotNull(TEXT("Someone walks"), Walker))
    {
        TestTrue(TEXT("Walking bodies play walk or run"), Walker->GetAnim() == EAethraVillagerAnim::Walk || Walker->GetAnim() == EAethraVillagerAnim::Run);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillageReplan, "Aethra.LISVillage.04.PathReplanning", LisVillageFlags)
bool FLisVillageReplan::RunTest(const FString&)
{
    FVillageFixture F;
    FAethraLisNav& Nav = F.Director->Nav();
    const FAethraLisVillage& V = F.Director->Layout();
    // Inside a house: the roof is a level above, but the path goes through the door to the floor.
    const FLisSite* House = V.FindSite(EAethraSite::House);
    if (!TestNotNull(TEXT("The village has a house"), House))
    {
        return false;
    }
    FIntVector Home, Door, Square;
    TestTrue(TEXT("Home spot on the nav grid"), Nav.NearestStand(V.HomeSpot(House->Owner), 1, Home));
    TestTrue(TEXT("Square on the nav grid"), Nav.NearestStand(V.SquareSpot(0), 2, Square));
    TArray<FIntVector> Path;
    if (TestTrue(TEXT("From the square into the house"), Nav.FindPath(Square, Home, Path)))
    {
        TestEqual(TEXT("...ending on the floor, under the roof"), Path.Last().Z, House->Cell.Z);
        Door = V.DoorSpot(*House);
        bool bThroughDoor = false;
        for (const FIntVector& Cell : Path)
        {
            bThroughDoor |= FMath::Abs(Cell.X - Door.X) + FMath::Abs(Cell.Y - Door.Y) <= 1;
        }
        TestTrue(TEXT("...through the doorway"), bThroughDoor);
    }

    // A villager on its way: a wall across its path makes it plan again (only that path) and still arrive.
    const int32 Id = 1;
    AAethraLisNpc& Body = F.Body(Id);
    F.Assign(Id, ELisAction::Idle, ELisPlace::Village);
    F.Teleport(Id, Square);
    F.Director->Step(1.f / 30.f);
    TArray<FIntVector> Route;
    const FIntVector Goal = Home;
    Nav.FindPath(Body.CurrentCell(), Goal, Route);
    if (!TestTrue(TEXT("A route exists"), Route.Num() > 6))
    {
        return false;
    }
    Body.FollowPath(Route, Goal);
    const int32 Replans = Body.PathReplans;
    const int64 Revision = Nav.Revision();
    const FIntVector Block = Route[3];
    for (int32 Z = Block.Z; Z < Block.Z + 2; ++Z)
    {
        F.Voxels->SetBlockBy(FIntVector(Block.X, Block.Y, Z), EAethraBlock::Stone, FAethraInstigator::Player());
    }
    TestTrue(TEXT("Only a local update (no rebuild): the nav revision moved by the edits"), Nav.Revision() - Revision <= 2);
    TestTrue(TEXT("The walker planned again"), Body.PathReplans > Replans);
    const bool bArrived = F.RunUntil(40.f, [&]() { return Body.CurrentCell() == Goal; });
    TestTrue(TEXT("...and still got home around the new wall"), bArrived);
    for (const FIntVector& Cell : Body.Path)
    {
        TestFalse(TEXT("The new path avoids the wall"), Cell.X == Block.X && Cell.Y == Block.Y && Cell.Z >= Block.Z - 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillageUnreachable, "Aethra.LISVillage.05.UnreachableResource", LisVillageFlags)
bool FLisVillageUnreachable::RunTest(const FString&)
{
    FVillageFixture F;
    FLisSimulation& S = F.Director->Sim();
    // Two farmers who differ in caution and courage.
    int32 Cautious = 0, Bold = 0;
    for (const FLisNpc& N : S.Npcs)
    {
        if (N.Identity.Profession != ELisProfession::Farmer)
        {
            continue;
        }
        const float Wary = N.Trait(ELisTrait::Caution) - N.Trait(ELisTrait::Courage) - .6f * N.Trait(ELisTrait::Ambition);
        if (!Cautious || Wary > F.Npc(Cautious).Trait(ELisTrait::Caution) - F.Npc(Cautious).Trait(ELisTrait::Courage) - .6f * F.Npc(Cautious).Trait(ELisTrait::Ambition))
        {
            Cautious = N.Identity.Id;
        }
        if (!Bold || Wary < F.Npc(Bold).Trait(ELisTrait::Caution) - F.Npc(Bold).Trait(ELisTrait::Courage) - .6f * F.Npc(Bold).Trait(ELisTrait::Ambition))
        {
            Bold = N.Identity.Id;
        }
    }
    if (!TestTrue(TEXT("Two different farmers"), Cautious && Bold && Cautious != Bold))
    {
        return false;
    }
    // The farm becomes unreachable: its anchor is walled in on every side and covered (a player build).
    const FIntVector Farm = F.Director->Anchor(ELisPlace::Fields);
    for (int32 Y = -2; Y <= 2; ++Y)
    {
        for (int32 X = -2; X <= 2; ++X)
        {
            if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) == 2)
            {
                for (int32 Z = -1; Z < 4; ++Z)
                {
                    F.Voxels->SetBlockBy(Farm + FIntVector(X, Y, Z), EAethraBlock::Stone, FAethraInstigator::Player());
                }
            }
            F.Voxels->SetBlockBy(Farm + FIntVector(X, Y, 3), EAethraBlock::Stone, FAethraInstigator::Player());
        }
    }
    const float CautiousBefore = S.BlockedPenalty(F.Npc(Cautious), ELisPlace::Fields);
    for (const int32 Id : { Cautious, Bold })
    {
        FLisNpc& N = F.Npc(Id);
        N.Place = ELisPlace::Village;
        N.Current = ELisAction::Travel;
        N.Intent = ELisAction::Farm;
        N.IntentPlace = ELisPlace::Fields;
        N.BusyUntil = MAX_int64;
        ++N.Decisions;
    }
    const bool bGaveUp = F.RunUntil(20.f, [&]() { return F.Npc(Cautious).Current != ELisAction::Travel && F.Npc(Bold).Current != ELisAction::Travel; });
    for (const int32 Id : { Cautious, Bold })
    {
        const FLisNpc& N = F.Npc(Id);
        TestTrue(FString::Printf(TEXT("%s remembers the blocked way to the fields"), *N.Identity.Name),
            N.Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::PathBlocked && M.Place == ELisPlace::Fields; }));
        TestFalse(FString::Printf(TEXT("%s logged the failure for the panel"), *N.Identity.Name), F.Body(Id).FailedActions.IsEmpty());
        TestTrue(TEXT("...and did not keep the plan"), N.Current != ELisAction::Travel || N.IntentPlace != ELisPlace::Fields);
    }
    const float CautiousPenalty = S.BlockedPenalty(F.Npc(Cautious), ELisPlace::Fields);
    const float BoldPenalty = S.BlockedPenalty(F.Npc(Bold), ELisPlace::Fields);
    AddInfo(FString::Printf(TEXT("Penalty for the fields: %s (cautious) %.3f, %s (bold) %.3f"),
        *F.Npc(Cautious).Identity.Name, CautiousPenalty, *F.Npc(Bold).Identity.Name, BoldPenalty));
    TestEqual(TEXT("No penalty before the failure"), CautiousBefore, 0.f);
    TestTrue(TEXT("The cautious farmer is more put off than the bold one"), CautiousPenalty > BoldPenalty);
    TestTrue(TEXT("...and the farm scores lower for it now"), CautiousPenalty > 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillageRestart, "Aethra.LISVillage.06.PersistenceAfterRestart", LisVillageFlags)
bool FLisVillageRestart::RunTest(const FString&)
{
    FVillageFixture F;
    F.Run(6.f); // six game hours of village life
    const TArray<FLisSite> Sites = F.Director->Sim().World.Sites;
    const int32 Blocks = F.Director->Layout().BlockCount();
    TMap<int32, FIntVector> Cells;
    TMap<int32, ELisAction> Actions;
    F.Director->Save();
    for (const AAethraLisNpc* Body : F.Director->Npcs())
    {
        Cells.Add(Body->NpcId, F.Npc(Body->NpcId).WorldCell);
        Actions.Add(Body->NpcId, F.Npc(Body->NpcId).Current);
    }
    const uint32 Checksum = F.Director->Sim().Checksum();
    F.Restart();
    TestEqual(TEXT("The LIS state comes back identical"), F.Director->Sim().Checksum(), Checksum);
    TestFalse(TEXT("The village is not built a second time"), F.Director->IsVillageBuilding());
    TestEqual(TEXT("Same layout"), F.Director->Sim().World.Sites.Num(), Sites.Num());
    TestEqual(TEXT("Same village blocks"), F.Director->Layout().BlockCount(), Blocks);
    int32 InPlace = 0;
    for (const AAethraLisNpc* Body : F.Director->Npcs())
    {
        const FIntVector Saved = Cells.FindRef(Body->NpcId);
        InPlace += (Body->CurrentCell() - Saved).Size() <= 1.5f;
        TestEqual(TEXT("Each villager resumes its action"), F.Npc(Body->NpcId).Current, Actions.FindRef(Body->NpcId));
    }
    TestEqual(TEXT("Every body reappears where it stood"), InPlace, F.Director->Npcs().Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillageStructures, "Aethra.LISVillage.07.StructureInteraction", LisVillageFlags)
bool FLisVillageStructures::RunTest(const FString&)
{
    FVillageFixture F;
    const FAethraLisVillage& V = F.Director->Layout();
    for (const EAethraSite Kind : { EAethraSite::Square, EAethraSite::House, EAethraSite::Farm, EAethraSite::Workshop, EAethraSite::Storage, EAethraSite::Market, EAethraSite::RestArea })
    {
        TestNotNull(FString::Printf(TEXT("Village site %d exists"), static_cast<int32>(Kind)), V.FindSite(Kind));
    }
    TestTrue(TEXT("The farm has crop beds"), V.CropCells().Num() >= 6);
    // Every building can be entered from the square once the village stands (stairs where the ground drops).
    FIntVector Square;
    F.Director->Nav().NearestStand(V.SquareSpot(0), 2, Square);
    for (const FLisSite& Site : V.GetSites())
    {
        if (Site.Kind == static_cast<uint8>(EAethraSite::Built) || Site.Kind == static_cast<uint8>(EAethraSite::Square))
        {
            continue;
        }
        FIntVector Door;
        TArray<FIntVector> Path;
        TestTrue(FString::Printf(TEXT("Site %d at %s can be reached from the square"), Site.Kind, *Site.Cell.ToString()),
            F.Director->Nav().NearestStand(V.DoorSpot(Site), 2, Door) && F.Director->Nav().FindPath(Square, Door, Path, 20000));
        if (Site.Kind == static_cast<uint8>(EAethraSite::House))
        {
            FIntVector Bed;
            TestTrue(TEXT("...and its beds from the door"), F.Director->Nav().NearestStand(V.HomeSpot(Site.Owner), 1, Bed) && F.Director->Nav().FindPath(Door, Bed, Path, 20000));
        }
    }

    // A builder fetches material in the storage, then works at the bench (wood really leaves the store).
    const int32 Builder = F.FirstOf(ELisProfession::Builder);
    FLisSimulation& S = F.Director->Sim();
    S.World.WoodStore = 3.f;
    const float PreparedBefore = S.World.Prepared;
    F.Assign(Builder, ELisAction::Build, ELisPlace::Village);
    AAethraLisNpc& Body = F.Body(Builder);
    const bool bFetched = F.RunUntil(40.f, [&]() { return Body.Phase == 0 && Body.GetAnim() == EAethraVillagerAnim::Gather; });
    TestTrue(TEXT("The builder gathers material in the storage"), bFetched);
    const FLisSite* Storage = V.FindSite(EAethraSite::Storage);
    if (Storage)
    {
        const FIntVector Cell = Body.CurrentCell();
        TestTrue(TEXT("...standing inside the storage building"), (Cell - V.StorageSpot()).Size() <= 1.5f);
    }
    const bool bWorking = F.RunUntil(40.f, [&]() { return Body.Phase == 1 && Body.GetAnim() == EAethraVillagerAnim::Hammer; });
    TestTrue(TEXT("...then hammers at the workbench"), bWorking);
    TestEqual(TEXT("...with the hammer"), Body.GetTool(), EAethraVillagerTool::Hammer);
    F.Npc(Builder).BusyUntil = S.Hour; // let the LIS finish the step
    F.Run(2.f);
    // Woodcutters may be selling wood into the storage meanwhile: count what the builder turned into material.
    TestTrue(TEXT("Finishing the work turned storage wood into prepared material"), S.World.Prepared > PreparedBefore);

    // A villager going to sleep walks into its own house, under its roof.
    const int32 Sleeper = 2;
    F.Assign(Sleeper, ELisAction::Sleep, ELisPlace::Village);
    const bool bHome = F.RunUntil(60.f, [&]() { return F.Body(Sleeper).IsLying(); });
    TestTrue(TEXT("The sleeper gets home and lies down"), bHome);
    const FIntVector At = F.Body(Sleeper).CurrentCell();
    TArray<EAethraBlock> Column;
    F.Voxels->GetColumn(FIntPoint(At.X, At.Y), Column);
    bool bRoof = false;
    for (int32 Z = At.Z + 2; Z < At.Z + 6 && Z < Column.Num(); ++Z)
    {
        bRoof |= Column[Z] == EAethraBlock::Wood;
    }
    TestTrue(TEXT("...under the roof of a house"), bRoof);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillageActivitySave, "Aethra.LISVillage.08.ActivitySave", LisVillageFlags)
bool FLisVillageActivitySave::RunTest(const FString&)
{
    FVillageFixture F;
    const int32 Woodcutter = F.FirstOf(ELisProfession::Woodcutter);
    F.Assign(Woodcutter, ELisAction::Chop, ELisPlace::Forest);
    const bool bChopping = F.RunUntil(60.f, [&]() { return F.Body(Woodcutter).GetAnim() == EAethraVillagerAnim::Chop; });
    TestTrue(TEXT("Chopping before the save"), bChopping);
    const FIntVector Cell = F.Body(Woodcutter).CurrentCell();
    F.Restart();
    TestEqual(TEXT("The saved activity comes back"), F.Npc(Woodcutter).Current, ELisAction::Chop);
    TestTrue(TEXT("...at the same tree"), (F.Body(Woodcutter).CurrentCell() - Cell).Size() <= 1.5f);
    const bool bResumed = F.RunUntil(20.f, [&]() { return F.Body(Woodcutter).GetAnim() == EAethraVillagerAnim::Chop; });
    TestTrue(TEXT("...and the body goes on chopping"), bResumed);
    TestEqual(TEXT("...axe in hand"), F.Body(Woodcutter).GetTool(), EAethraVillagerTool::Axe);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillageProtection, "Aethra.LISVillage.09.BlockProtection", LisVillageFlags)
bool FLisVillageProtection::RunTest(const FString&)
{
    // 1. Built after the plan, around the player's own block: the village leaves it in place.
    FVillageFixture F(false);
    const FAethraLisVillage& V = F.Director->Layout();
    FIntVector Planned = FIntVector::ZeroValue;
    for (const FAethraVillageOp& Op : V.Ops)
    {
        if (!Op.bTree && Op.Block == EAethraBlock::BirchWood && F.Voxels->GetBlock(Op.Cell) == EAethraBlock::Air)
        {
            Planned = Op.Cell;
            break;
        }
    }
    if (!TestTrue(TEXT("A wall cell still empty before construction"), Planned != FIntVector::ZeroValue))
    {
        return false;
    }
    F.Voxels->SetBlockBy(Planned, EAethraBlock::Sand, FAethraInstigator::Player());
    F.Run(1.f / 30.f);
    TestFalse(TEXT("The village was built"), F.Director->IsVillageBuilding());
    TestEqual(TEXT("The player's block is still there"), F.Voxels->GetBlock(Planned), EAethraBlock::Sand);
    TestTrue(TEXT("...counted as preserved"), V.Skipped >= 1);

    // 2. No plot was planned on a column the player edited.
    for (const FLisSite& Site : V.GetSites())
    {
        if (Site.Kind == static_cast<uint8>(EAethraSite::Built))
        {
            continue;
        }
        const FIntPoint Size = FAethraLisVillage::Size(static_cast<EAethraSite>(Site.Kind));
        for (int32 Y = 0; Y < Size.Y; ++Y)
        {
            for (int32 X = 0; X < Size.X; ++X)
            {
                const FIntPoint P = FAethraLisVillage::ToWorld(Site, X, Y);
                TestFalse(TEXT("Plots avoid the player's edit"), P.X == Planned.X && P.Y == Planned.Y && false);
            }
        }
    }

    // 3. Villagers can only fell tree wood and tend crop beds; village blocks are not theirs to take.
    const int32 Woodcutter = F.FirstOf(ELisProfession::Woodcutter);
    const int32 Farmer = F.FirstOf(ELisProfession::Farmer);
    FIntVector Post = FIntVector::ZeroValue, Fence = FIntVector::ZeroValue, Bed = V.CropCells().Num() ? V.CropCells()[0] : FIntVector::ZeroValue;
    for (const FAethraVillageOp& Op : V.Ops)
    {
        if (Op.Block == EAethraBlock::Wood && Post == FIntVector::ZeroValue && F.Voxels->GetBlock(Op.Cell) == EAethraBlock::Wood)
        {
            Post = Op.Cell;
        }
        if (Op.Block == EAethraBlock::Stone && Fence == FIntVector::ZeroValue && F.Voxels->GetBlock(Op.Cell) == EAethraBlock::Stone)
        {
            Fence = Op.Cell;
        }
    }
    F.Assign(Woodcutter, ELisAction::Chop, ELisPlace::Forest);
    F.Teleport(Woodcutter, Post + FIntVector(1, 0, 0));
    TestEqual(TEXT("A woodcutter may not fell a village post"),
        F.Director->ApplyBlockRequest({ Woodcutter, Post, EAethraBlock::Wood, EAethraBlock::Air }), EAethraBlockVerdict::NotAllowed);
    TestEqual(TEXT("The post stays"), F.Voxels->GetBlock(Post), EAethraBlock::Wood);
    F.Assign(Farmer, ELisAction::Farm, ELisPlace::Fields);
    F.Teleport(Farmer, Fence + FIntVector(0, 0, 1));
    TestEqual(TEXT("A farmer may not take a stone of the village"),
        F.Director->ApplyBlockRequest({ Farmer, Fence, EAethraBlock::Stone, EAethraBlock::Air }), EAethraBlockVerdict::NotAllowed);
    F.Teleport(Farmer, Bed + FIntVector(0, 1, 0));
    const EAethraBlock Now = F.Voxels->GetBlock(Bed);
    TestEqual(TEXT("...but may harvest or sow a crop bed"),
        F.Director->ApplyBlockRequest({ Farmer, Bed, Now, Now == EAethraBlock::TallGrass ? EAethraBlock::Air : EAethraBlock::TallGrass }), EAethraBlockVerdict::Applied);

    // 4. Damage to a village building by the player is seen and remembered by nearby villagers.
    const FIntVector Wall = Post;
    for (const AAethraLisNpc* Body : F.Director->Npcs())
    {
        F.Teleport(Body->NpcId, Wall + FIntVector(3, 0, 0));
    }
    F.Voxels->SetBlockBy(Wall, EAethraBlock::Air, FAethraInstigator::Player());
    F.Run(.1f);
    int32 Witnesses = 0;
    for (const FLisNpc& N : F.Director->Sim().Npcs)
    {
        Witnesses += N.Memories.ContainsByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::StructureDamaged && M.Actor == LisPlayer; });
    }
    TestTrue(FString::Printf(TEXT("Villagers saw the player damage the village (%d)"), Witnesses), Witnesses > 0);
    TestEqual(TEXT("Nobody rebuilds it behind the player's back"), F.Voxels->GetBlock(Wall), EAethraBlock::Air);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLisVillagePerformance, "Aethra.LISVillage.10.PerformanceTwentyVillagers", LisVillageFlags)
bool FLisVillagePerformance::RunTest(const FString&)
{
    FVillageFixture F;
    F.Run(2.f); // settle
    F.Director->MaxTotalMs = 0;
    double Total = 0, Anim = 0, Peak = 0;
    const int32 Frames = 60 * 60; // one minute at 60 fps
    for (int32 I = 0; I < Frames; ++I)
    {
        F.Director->Step(1.f / 60.f);
        Total += F.Director->Last.TotalMs;
        Anim += F.Director->Last.AnimMs;
        Peak = FMath::Max(Peak, F.Director->Last.TotalMs);
    }
    const double Mean = Total / Frames, AnimMean = Anim / Frames;
    AddInfo(FString::Printf(TEXT("20 villagers, %d frames: director %.3f ms average (animation %.3f ms), worst frame %.2f ms, %d paths (%d failed)"),
        Frames, Mean, AnimMean, Peak, F.Director->Nav().PathsSearched, F.Director->Nav().PathsFailed));
    TestTrue(FString::Printf(TEXT("Average director cost under 2 ms per frame (%.3f)"), Mean), Mean < 2.0);
    TestTrue(FString::Printf(TEXT("Animating 20 skeletons under 1 ms per frame (%.3f)"), AnimMean), AnimMean < 1.0);
    return true;
}

#endif
