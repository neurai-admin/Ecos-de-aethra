#if WITH_DEV_AUTOMATION_TESTS

#include "AethraVoxelWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Voxel/AethraTerrain.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVoxelPersistAcrossRestart, "Aethra.Voxel.PersistAcrossRestart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    constexpr int32 TestSeed = 990417;

    void DeleteTestSlots()
    {
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            const FString Name = FString::Printf(TEXT("AethraWorld_%d_%d"), TestSeed, Slot);
            if (UGameplayStatics::DoesSaveGameExist(Name, 0))
            {
                UGameplayStatics::DeleteGameInSlot(Name, 0);
            }
        }
    }

    // Each session gets a fresh UWorld, so only the on-disk save carries state between them.
    template <typename FBody>
    void RunSession(FBody&& Body, int32 Seed = TestSeed)
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());

        FActorSpawnParameters Params;
        Params.bDeferConstruction = true;
        auto* Voxels = World->SpawnActor<AAethraVoxelWorld>(Params);
        Voxels->Seed = Seed;
        Voxels->FinishSpawning(FTransform::Identity);
        Voxels->PrepareSpawn(); // the game's entry point: loads saved edits and the centre chunk
        Body(*Voxels);

        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }
}

bool FAethraVoxelPersistAcrossRestart::RunTest(const FString& Parameters)
{
    DeleteTestSlots();

    // Chunk (-1, 0) border cell, so the edit also exercises negative coordinates.
    const FIntPoint Column(-16, 3);
    const int32 Surface = AethraTerrain::SurfaceAt(Column, TestSeed);
    const FIntVector Removed(Column.X, Column.Y, Surface);
    const FIntVector Placed(Column.X, Column.Y, Surface + 5); // Surface <= 56, stays below Height

    EAethraBlock Original = EAethraBlock::Air;
    RunSession([&](AAethraVoxelWorld& Voxels)
    {
        Original = Voxels.GetBlock(Removed);
        TestNotEqual(TEXT("Surface cell is solid"), Original, EAethraBlock::Air);
        TestTrue(TEXT("Removal accepted"), Voxels.SetBlock(Removed, EAethraBlock::Air));
        TestTrue(TEXT("Placement accepted"), Voxels.SetBlock(Placed, EAethraBlock::Stone));
    });

    RunSession([&](AAethraVoxelWorld& Voxels)
    {
        TestEqual(TEXT("Removal survives restart"), Voxels.GetBlock(Removed), EAethraBlock::Air);
        TestEqual(TEXT("Placement survives restart"), Voxels.GetBlock(Placed), EAethraBlock::Stone);
        TestTrue(TEXT("Restoring the generated block clears the override"), Voxels.SetBlock(Removed, Original));
    });

    RunSession([&](AAethraVoxelWorld& Voxels)
    {
        TestEqual(TEXT("Restored block survives restart"), Voxels.GetBlock(Removed), Original);
        TestEqual(TEXT("Other edit still present"), Voxels.GetBlock(Placed), EAethraBlock::Stone);
    });

    TStaticArray<EAethraBlock, AethraTerrain::Height> OtherSeed;
    AethraTerrain::GenerateColumn(Column, TestSeed + 1, OtherSeed);
    RunSession([&](AAethraVoxelWorld& Voxels)
    {
        TestEqual(TEXT("Another seed does not see these edits"), Voxels.GetBlock(Placed), OtherSeed[Placed.Z]);
    }, TestSeed + 1);

    DeleteTestSlots();
    return true;
}

#endif
