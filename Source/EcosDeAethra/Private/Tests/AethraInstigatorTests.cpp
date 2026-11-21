#if WITH_DEV_AUTOMATION_TESTS

#include "AethraBlockChangeRecorder.h"
#include "AethraVoxelWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Voxel/AethraTerrain.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVoxelInstigator, "Aethra.Voxel.InstigatorCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraVoxelInstigator::RunTest(const FString& Parameters)
{
    using namespace AethraTerrain;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };

    FActorSpawnParameters Params;
    Params.bDeferConstruction = true;
    auto* Voxels = World->SpawnActor<AAethraVoxelWorld>(Params);
    if (!TestNotNull(TEXT("Voxel world spawned"), Voxels))
    {
        return false;
    }
    Voxels->Seed = static_cast<int32>(FGuid::NewGuid().B & 0x7fffffffu);
    // Same slot names as AAethraVoxelWorld::SaveSlot.
    const FString Slot0 = FString::Printf(TEXT("AethraWorld_%d_0"), Voxels->Seed);
    const FString Slot1 = FString::Printf(TEXT("AethraWorld_%d_1"), Voxels->Seed);
    ON_SCOPE_EXIT
    {
        UGameplayStatics::DeleteGameInSlot(Slot0, 0);
        UGameplayStatics::DeleteGameInSlot(Slot1, 0);
    };
    Voxels->FinishSpawning(FTransform::Identity);

    auto* Recorder = NewObject<UAethraBlockChangeRecorder>();
    Voxels->OnBlockChanged.AddDynamic(Recorder, &UAethraBlockChangeRecorder::Record);

    constexpr int32 Z = Height - 4; // clear air above any procedural surface
    const FIntVector A(3, 4, Z), B(5, 4, Z), C(7, 4, Z);
    TestTrue(TEXT("The original SetBlock still works"), Voxels->SetBlock(A, EAethraBlock::Stone));
    TestTrue(TEXT("SetBlockBy with a player"), Voxels->SetBlockBy(B, EAethraBlock::Sand, FAethraInstigator::Player()));
    TestTrue(TEXT("SetBlockBy with an NPC"), Voxels->SetBlockBy(C, EAethraBlock::Dirt, FAethraInstigator::Npc(17)));
    TestFalse(TEXT("SetBlockBy keeps the same validation (outside the world)"),
        Voxels->SetBlockBy(FIntVector(0, 0, Height + 5), EAethraBlock::Dirt, FAethraInstigator::Npc(17)));
    TestFalse(TEXT("...and refuses a no-op change"), Voxels->SetBlockBy(C, EAethraBlock::Dirt, FAethraInstigator::Npc(17)));

    if (TestEqual(TEXT("Three accepted changes were broadcast"), Recorder->Changes.Num(), 3))
    {
        TestEqual(TEXT("Old callers report an unknown author"), Recorder->Changes[0].Instigator.Kind, EAethraInstigatorKind::Unknown);
        TestEqual(TEXT("...with id 0"), Recorder->Changes[0].Instigator.Id, 0);
        TestEqual(TEXT("Player edits are attributed"), Recorder->Changes[1].Instigator.Kind, EAethraInstigatorKind::Player);
        TestEqual(TEXT("NPC edits carry the persistent id"), Recorder->Changes[2].Instigator.Id, 17);
        TestTrue(TEXT("Cell, Before and After are unchanged"), Recorder->Changes[2].Cell == C
            && Recorder->Changes[2].Before == EAethraBlock::Air && Recorder->Changes[2].After == EAethraBlock::Dirt);
    }

    // GetColumn agrees with GetBlock, edits included.
    TArray<EAethraBlock> Column;
    Voxels->GetColumn(FIntPoint(C.X, C.Y), Column);
    bool bSame = Column.Num() == Height;
    for (int32 Level = 0; bSame && Level < Height; ++Level)
    {
        bSame = Column[Level] == Voxels->GetBlock(FIntVector(C.X, C.Y, Level));
    }
    TestTrue(TEXT("GetColumn matches GetBlock cell by cell, including edits"), bSame);
    Voxels->GetColumn(FIntPoint(HorizontalLimit + 10, 0), Column);
    TestTrue(TEXT("Outside the world the column is air"), !Column.ContainsByPredicate([](EAethraBlock Block) { return Block != EAethraBlock::Air; }));

    // The voxel save format is untouched: a fresh world restores the same blocks from disk.
    FActorSpawnParameters Again;
    Again.bDeferConstruction = true;
    auto* Restored = World->SpawnActor<AAethraVoxelWorld>(Again);
    Restored->Seed = Voxels->Seed;
    Restored->FinishSpawning(FTransform::Identity);
    Restored->PrepareSpawn(); // public entry that loads the saved edits
    TestEqual(TEXT("Saved edit restored (unknown author)"), Restored->GetBlock(A), EAethraBlock::Stone);
    TestEqual(TEXT("Saved edit restored (player)"), Restored->GetBlock(B), EAethraBlock::Sand);
    TestEqual(TEXT("Saved edit restored (NPC)"), Restored->GetBlock(C), EAethraBlock::Dirt);
    return true;
}

#endif
