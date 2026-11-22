#if WITH_DEV_AUTOMATION_TESTS

#include "AethraVoxelWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "VisualWorld/AethraBlockGen.h"
#include "Voxel/AethraTerrain.h"

// Names prefixed (Bw...) because the unity build merges test files.
namespace
{
constexpr int32 BwWorldSeed = 125; // the Visual World demo seed: river by the spawn
const int32 BwSeed = FAethraBlockGen::MakeSeed(BwWorldSeed);

using FBwColumn = TStaticArray<EAethraBlock, AethraTerrain::Height>;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraBlockWorldSeeds, "Aethra.BlockWorld.SeedsKeepTheirGenerator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraBlockWorldSeeds::RunTest(const FString& Parameters)
{
    // Old worlds (default 42819, the Elden demo, test seeds) never reach the block world.
    for (const int32 Seed : { 42819, 990417, 1, 0x3FFFFFFF, -5 })
    {
        TestFalse(FString::Printf(TEXT("Seed %d keeps the original generator"), Seed), FAethraBlockGen::IsAethraSeed(Seed));
    }
    TestTrue(TEXT("Flagged seed is the block world"), FAethraBlockGen::IsAethraSeed(BwSeed));
    TestEqual(TEXT("World seed round-trips"), FAethraBlockGen::WorldSeedOf(BwSeed), BwWorldSeed);

    // The original generator only produces the original blocks.
    FBwColumn Column;
    for (int32 I = 0; I < 64; ++I)
    {
        AethraTerrain::GenerateColumn(FIntPoint(I * 37 - 900, I * 53 - 700), 42819, Column);
        for (const EAethraBlock B : Column)
        {
            if (static_cast<uint8>(B) > static_cast<uint8>(EAethraBlock::BirchLeaves))
            {
                AddError(TEXT("Original generator emitted a block-world id"));
                return false;
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraBlockWorldColumns, "Aethra.BlockWorld.ColumnsAreBlocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraBlockWorldColumns::RunTest(const FString& Parameters)
{
    const FAethraBlockGen& Gen = FAethraBlockGen::Get(BwSeed);
    const FAethraBlockGen Again(BwWorldSeed);
    FBwColumn A, B;
    int32 Water = 0, Plants = 0, Trees = 0, Columns = 0, SolidTops = 0;
    for (int32 Y = -96; Y < 96; Y += 3)
    {
        for (int32 X = -96; X < 96; X += 3)
        {
            const FIntPoint P(X, Y);
            Gen.GenerateColumn(P, A);
            Again.GenerateColumn(P, B);
            const int32 Ground = Gen.SurfaceAt(P);
            if (!TestTrue(TEXT("Surface in range"), Ground >= 2 && Ground < AethraTerrain::Height - 7))
            {
                return false;
            }
            for (int32 Z = 0; Z < AethraTerrain::Height; ++Z)
            {
                if (A[Z] != B[Z])
                {
                    AddError(FString::Printf(TEXT("Not deterministic at (%d, %d, %d)"), X, Y, Z));
                    return false;
                }
                if (!AethraTerrain::IsKnownBlock(A[Z]))
                {
                    AddError(TEXT("Unknown block id"));
                    return false;
                }
                Water += A[Z] == EAethraBlock::Water;
                Plants += AethraTerrain::IsPlant(A[Z]);
                Trees += A[Z] == EAethraBlock::Wood || A[Z] == EAethraBlock::BirchWood;
            }
            TestEqual(TEXT("Bedrock layer is solid"), AethraTerrain::IsOpaque(A[0]), true);
            ++Columns;
            SolidTops += AethraTerrain::IsOpaque(A[Ground]); // not where a cave mouth opens
        }
    }
    TestTrue(TEXT("Surface blocks are solid"), SolidTops > Columns * 95 / 100);
    TestTrue(TEXT("Water near the spawn (river)"), Water > 0);
    TestTrue(TEXT("Flowers and grass"), Plants > 0);
    TestTrue(TEXT("Trees"), Trees > 0);
    AddInfo(FString::Printf(TEXT("water %d, plants %d, trunk blocks %d"), Water, Plants, Trees));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraBlockWorldRiver, "Aethra.BlockWorld.RiversAreWaterBlocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraBlockWorldRiver::RunTest(const FString& Parameters)
{
    const FAethraBlockGen& Gen = FAethraBlockGen::Get(BwSeed);
    const FAethraWorldGen& World = Gen.World();
    FBwColumn Column;
    int32 Checked = 0;
    for (int32 Y = -400; Y < 400 && Checked < 40; Y += 5)
    {
        for (int32 X = -400; X < 400 && Checked < 40; X += 5)
        {
            const FWorldSample S = World.Sample(FVector2D(X + .5, Y + .5));
            if (!S.IsWater() || !World.GetBodies().IsValidIndex(S.WaterBody) || World.GetBodies()[S.WaterBody].Kind != EHydroKind::River)
            {
                continue;
            }
            ++Checked;
            Gen.GenerateColumn(FIntPoint(X, Y), Column);
            const int32 Ground = Gen.SurfaceAt(FIntPoint(X, Y));
            TestEqual(TEXT("River water starts right above the bed"), Column[Ground + 1], EAethraBlock::Water);
            TestTrue(TEXT("River bed is gravel (or a carved cave)"), Column[Ground] == EAethraBlock::Gravel || Column[Ground] == EAethraBlock::Water || Column[Ground] == EAethraBlock::Air);
            for (int32 Z = Ground + 1; Z < AethraTerrain::Height; ++Z)
            {
                if (AethraTerrain::IsPlant(Column[Z]))
                {
                    AddError(TEXT("A plant grows in the river"));
                    return false;
                }
            }
        }
    }
    TestTrue(TEXT("Found river columns near the spawn"), Checked > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraBlockWorldDig, "Aethra.BlockWorld.DigAndPlaceAreBlocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraBlockWorldDig::RunTest(const FString& Parameters)
{
    // Its own flagged seed: the player's block-world save is never touched.
    const int32 Seed = FAethraBlockGen::MakeSeed(990125);
    const auto DeleteSlots = [Seed]()
    {
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            const FString Name = FString::Printf(TEXT("AethraWorld_%d_%d"), Seed, Slot);
            if (UGameplayStatics::DoesSaveGameExist(Name, 0))
            {
                UGameplayStatics::DeleteGameInSlot(Name, 0);
            }
        }
    };
    const auto Session = [Seed](TFunctionRef<void(AAethraVoxelWorld&)> Body)
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
        Voxels->PrepareSpawn();
        Body(*Voxels);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };
    DeleteSlots();

    const FIntPoint Spawn = FAethraBlockGen::Get(Seed).SpawnPoint();
    const int32 Ground = AethraTerrain::SurfaceAt(Spawn, Seed);
    const FIntVector Dug(Spawn.X, Spawn.Y, Ground);
    const FIntVector Placed(Spawn.X + 1, Spawn.Y, AethraTerrain::SurfaceAt(Spawn + FIntPoint(1, 0), Seed) + 1);
    Session([&](AAethraVoxelWorld& Voxels)
    {
        TestTrue(TEXT("Spawn ground is a solid block"), AethraTerrain::IsOpaque(Voxels.GetBlock(Dug)));
        TestTrue(TEXT("Dig accepted"), Voxels.SetBlock(Dug, EAethraBlock::Air));
        TestTrue(TEXT("Place accepted"), Voxels.SetBlock(Placed, EAethraBlock::StoneBricks));
        TestEqual(TEXT("Dug cell is exactly one empty block"), Voxels.GetBlock(Dug), EAethraBlock::Air);
        TestTrue(TEXT("The block below is untouched"), AethraTerrain::IsOpaque(Voxels.GetBlock(Dug - FIntVector(0, 0, 1))));
    });
    Session([&](AAethraVoxelWorld& Voxels)
    {
        TestEqual(TEXT("Dig survives restart"), Voxels.GetBlock(Dug), EAethraBlock::Air);
        TestEqual(TEXT("Placed block survives restart"), Voxels.GetBlock(Placed), EAethraBlock::StoneBricks);
    });
    DeleteSlots();
    return true;
}

#endif
