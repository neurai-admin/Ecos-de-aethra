#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Voxel/AethraTerrain.h"

using namespace AethraTerrain;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVoxelCoordinates, "Aethra.Voxel.Coordinates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraVoxelCoordinates::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Negative positions use floor division"), ChunkAt(FIntVector(-1, -17, 0)) == FIntPoint(-1, -2));
    TestTrue(TEXT("Exact negative boundary"), ChunkAt(FIntVector(-16, -32, 0)) == FIntPoint(-1, -2));
    TestTrue(TEXT("Positive boundary"), ChunkAt(FIntVector(16, 31, 0)) == FIntPoint(1, 1));
    TestTrue(TEXT("World-to-cell conversion floors"), CellAt(FVector(-0.1, 99.99, 100.0)) == FIntVector(-1, 0, 1));
    TestEqual(TEXT("Last cell fits dense array"), Index(FIntVector(ChunkSize - 1, ChunkSize - 1, Height - 1)), CellCount - 1);
    TestTrue(TEXT("Negative world edge included"), Contains(FIntVector(-HorizontalLimit, 0, 0)));
    TestFalse(TEXT("Positive world edge excluded"), Contains(FIntVector(HorizontalLimit, 0, 0)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVoxelGeneration, "Aethra.Voxel.Generation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraVoxelGeneration::RunTest(const FString& Parameters)
{
    const FIntPoint Chunk(-1, 0);
    constexpr int32 Seed = 42819;
    TArray<EAethraBlock> First, Repeat, Other;
    GenerateChunk(Chunk, Seed, First);
    GenerateChunk(Chunk, Seed, Repeat);
    GenerateChunk(Chunk, Seed + 1, Other);
    TestEqual(TEXT("Chunk has the expected size"), First.Num(), CellCount);
    TestTrue(TEXT("Same chunk and seed reproduce the same data"), First == Repeat);
    TestTrue(TEXT("A different seed changes generated data"), First != Other);

    const FIntVector Origin = ChunkOrigin(Chunk);
    for (int32 Y = 0; Y < ChunkSize; ++Y)
    {
        TStaticArray<EAethraBlock, Height> Column;
        GenerateColumn(FIntPoint(Origin.X + ChunkSize - 1, Origin.Y + Y), Seed, Column);
        for (int32 Z = 0; Z < Height; ++Z)
        {
            if (First[Index(FIntVector(ChunkSize - 1, Y, Z))] != Column[Z])
            {
                AddError(TEXT("A border cell differs between a full chunk and an isolated column."));
                return false;
            }
        }
        if (Column[0] != EAethraBlock::Stone)
        {
            AddError(TEXT("Bedrock is missing from a generated column."));
            return false;
        }
    }
    return true;
}

#endif
