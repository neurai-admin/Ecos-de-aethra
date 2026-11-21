#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Voxel/AethraHorizon.h"
#include "Voxel/AethraTerrain.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVoxelHorizon, "Aethra.Voxel.Horizon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraVoxelHorizon::RunTest(const FString& Parameters)
{
    using namespace AethraTerrain;
    constexpr int32 Seed = 42819;
    const FIntPoint Tile(-1, 0);
    const FIntPoint Base = Tile * AAethraHorizon::TileCells;

    const auto A = AAethraHorizon::BuildTile(Seed, Tile, 4, FIntRect());
    const auto B = AAethraHorizon::BuildTile(Seed, Tile, 4, FIntRect());
    TestTrue(TEXT("Tile has geometry"), A.Vertices.Num() > 0);
    TestTrue(TEXT("Same seed and tile reproduce the same mesh"), A.Vertices == B.Vertices && A.Colors == B.Colors);
    TestEqual(TEXT("Quads are complete"), A.Triangles.Num() * 2, A.Vertices.Num() * 3);

    // The first quad is the top of the first LOD cell and must sit on the voxel surface.
    TStaticArray<EAethraBlock, Height> Column;
    GenerateColumn(Base, Seed, Column);
    int32 Top = Height - 1;
    while (Top > 0 && !IsSurface(Column[Top]))
    {
        --Top;
    }
    TestEqual(TEXT("LOD top matches the voxel column"), A.Vertices[0].Z, (Top + 1) * CellSizeCm);

    const FIntRect Whole(Base, Base + FIntPoint(AAethraHorizon::TileCells));
    TestEqual(TEXT("Fully covered tile is empty"), AAethraHorizon::BuildTile(Seed, Tile, 16, Whole).Vertices.Num(), 0);

    const FIntRect Hole(Base, Base + FIntPoint(ChunkSize));
    const auto Holed = AAethraHorizon::BuildTile(Seed, Tile, 2, Hole);
    for (const FVector& V : Holed.Vertices)
    {
        const bool bInside = V.X > Hole.Min.X * CellSizeCm && V.X < Hole.Max.X * CellSizeCm
            && V.Y > Hole.Min.Y * CellSizeCm && V.Y < Hole.Max.Y * CellSizeCm;
        if (bInside)
        {
            AddError(FString::Printf(TEXT("Horizon vertex inside the voxel hole at %s"), *V.ToString()));
            return false;
        }
    }
    TestEqual(TEXT("Coarse rings use larger steps"), AAethraHorizon::StepFor(AAethraHorizon::RadiusTiles), 16);
    return true;
}

#endif
