#include "Voxel/AethraHorizon.h"

#include "VisualWorld/AethraBlockGen.h"

#include "Async/Async.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/AethraBlockColor.h"
#include "Voxel/AethraStats.h"
#include "Voxel/AethraTerrain.h"

using namespace AethraTerrain;

namespace
{
    constexpr int32 MaxJobs = 6;
    constexpr int32 MaxAppliesPerFrame = 1; // a step-2 tile upload costs up to ~5 ms
    constexpr int32 TileSkirtCells = 8; // hides cracks between LOD steps of neighbouring tiles

    int32 RingOf(FIntPoint Offset)
    {
        return FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y));
    }

    FIntRect Clip(const FIntRect& A, const FIntRect& B)
    {
        const FIntRect R(FIntPoint(FMath::Max(A.Min.X, B.Min.X), FMath::Max(A.Min.Y, B.Min.Y)),
            FIntPoint(FMath::Min(A.Max.X, B.Max.X), FMath::Min(A.Max.Y, B.Max.Y)));
        return R.Min.X < R.Max.X && R.Min.Y < R.Max.Y ? R : FIntRect();
    }
}

AAethraHorizon::AAethraHorizon()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Voxel(
        TEXT("/Game/Materials/M_AethraVoxel.M_AethraVoxel"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Fallback(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    Material = Voxel.Get() ? Voxel.Get() : Fallback.Get();
}

int32 AAethraHorizon::StepFor(int32 RingDistance)
{
    return RingDistance <= 1 ? 2 : RingDistance <= 3 ? 4 : RingDistance <= 5 ? 8 : 16;
}

AAethraHorizon::FMeshData AAethraHorizon::BuildTile(int32 Seed, FIntPoint Tile, int32 Step, const FIntRect& Hole)
{
    check(Step > 0 && TileCells % Step == 0 && ChunkSize % Step == 0);
    // shortcut: player edits are ignored here; they only matter inside the voxel radius. Upgrade if
    // large far-away builds must show on the horizon.
    const int32 Count = TileCells / Step;
    const int32 Row = Count + 2; // one sample of border on each side for neighbour heights
    const FIntPoint Base = Tile * TileCells;

    TArray<int32> Heights;
    TArray<EAethraBlock> Tops;
    Heights.SetNumUninitialized(Row * Row);
    Tops.SetNumUninitialized(Row * Row);
    TStaticArray<EAethraBlock, Height> Column;
    const bool bBlockWorld = FAethraBlockGen::IsAethraSeed(Seed);
    for (int32 J = -1; J <= Count; ++J)
    {
        for (int32 I = -1; I <= Count; ++I)
        {
            GenerateColumn(Base + FIntPoint(I, J) * Step, Seed, Column);
            int32 Top = Height - 1;
            // Block world: far trees are not boxes on the horizon; a canopy only tints the ground below.
            bool bCanopy = false;
            while (Top >= 0 && (!IsSurface(Column[Top]) // plants are too small for the horizon
                || (bBlockWorld && (IsLeaves(Column[Top]) || Column[Top] == EAethraBlock::Wood || Column[Top] == EAethraBlock::BirchWood))))
            {
                bCanopy |= IsLeaves(Column[Top]);
                --Top;
            }
            const int32 Slot = (J + 1) * Row + I + 1;
            Heights[Slot] = Top + 1; // 0 = no ground (outside the world)
            Tops[Slot] = Top >= 0 ? (bCanopy ? EAethraBlock::Leaves : Column[Top]) : EAethraBlock::Air;
        }
    }

    const auto InHole = [&Hole](FIntPoint Cell)
    {
        return Cell.X >= Hole.Min.X && Cell.X < Hole.Max.X && Cell.Y >= Hole.Min.Y && Cell.Y < Hole.Max.Y;
    };

    FMeshData Mesh;
    // Same vertex order and winding as the chunk faces (AethraVoxelChunk.cpp).
    const auto AddQuad = [&Mesh](const FVector& O, const FVector& A, const FVector& B, const FVector& Normal,
        const FLinearColor& Color)
    {
        const int32 Start = Mesh.Vertices.Num();
        Mesh.Vertices.Append({ O, O + A, O + A + B, O + B });
        Mesh.Triangles.Append({ Start, Start + 1, Start + 2, Start, Start + 2, Start + 3 });
        for (int32 Vertex = 0; Vertex < 4; ++Vertex)
        {
            Mesh.Normals.Add(Normal);
            Mesh.Colors.Add(Color);
        }
    };

    static const FIntPoint Sides[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    const double Size = Step * CellSizeCm;
    for (int32 J = 0; J < Count; ++J)
    {
        for (int32 I = 0; I < Count; ++I)
        {
            const FIntPoint Cell = Base + FIntPoint(I, J) * Step;
            const int32 Slot = (J + 1) * Row + I + 1;
            const int32 Top = Heights[Slot];
            if (Top == 0 || InHole(Cell))
            {
                continue;
            }

            const FIntVector Surface(Cell.X, Cell.Y, Top - 1);
            const FVector Corner(Cell.X * CellSizeCm, Cell.Y * CellSizeCm, 0.0);
            AddQuad(Corner + FVector(0, 0, Top * CellSizeCm), FVector(0, Size, 0), FVector(Size, 0, 0),
                FVector::UpVector, AethraBlockColor(Tops[Slot], Surface, FIntVector(0, 0, 1)));

            for (const FIntPoint Side : Sides)
            {
                const int32 NI = I + Side.X;
                const int32 NJ = J + Side.Y;
                int32 Bottom = Heights[(NJ + 1) * Row + NI + 1];
                if (InHole(Cell + Side * Step))
                {
                    // Voxel chunks can sit far below the coarse sample; close the edge down to bedrock.
                    Bottom = 0;
                }
                else if (NI < 0 || NI >= Count || NJ < 0 || NJ >= Count)
                {
                    Bottom = FMath::Min(Bottom, Top) - TileSkirtCells;
                }
                Bottom = FMath::Max(Bottom, 0);
                if (Bottom >= Top)
                {
                    continue;
                }

                const FVector Low = Corner + FVector(0, 0, Bottom * CellSizeCm);
                const FVector Rise(0, 0, (Top - Bottom) * CellSizeCm);
                const FIntVector Normal(Side.X, Side.Y, 0);
                const FLinearColor Color = AethraBlockColor(Tops[Slot], Surface, Normal);
                if (Side.X > 0)      AddQuad(Low + FVector(Size, 0, 0), Rise, FVector(0, Size, 0), FVector(Normal), Color);
                else if (Side.X < 0) AddQuad(Low, FVector(0, Size, 0), Rise, FVector(Normal), Color);
                else if (Side.Y > 0) AddQuad(Low + FVector(0, Size, 0), FVector(Size, 0, 0), Rise, FVector(Normal), Color);
                else                 AddQuad(Low, Rise, FVector(Size, 0, 0), FVector(Normal), Color);
            }
        }
    }
    return Mesh;
}

void AAethraHorizon::Update(int32 Seed, FIntPoint CenterChunk, int32 NearRadius)
{
    CSV_SCOPED_TIMING_STAT(Aethra, HorizonUpdate);
    constexpr int32 ChunksPerTile = TileCells / ChunkSize;
    const FIntPoint CenterTile(FloorDiv(CenterChunk.X, ChunksPerTile), FloorDiv(CenterChunk.Y, ChunksPerTile));
    const FIntRect Near((CenterChunk - FIntPoint(NearRadius)) * ChunkSize,
        (CenterChunk + FIntPoint(NearRadius + 1)) * ChunkSize);

    // One tile of hysteresis; a running job keeps going in the pool and its result is dropped.
    for (auto It = Tiles.CreateIterator(); It; ++It)
    {
        if (RingOf(It.Key() - CenterTile) > RadiusTiles + 1)
        {
            TObjectPtr<UProceduralMeshComponent> Mesh;
            if (Meshes.RemoveAndCopyValue(It.Key(), Mesh) && Mesh)
            {
                Mesh->DestroyComponent();
            }
            It.RemoveCurrent();
        }
    }

    int32 InFlight = 0;
    int32 Applied = 0;
    for (auto It = Tiles.CreateIterator(); It; ++It)
    {
        FTileState& State = It.Value();
        if (!State.Job.IsSet())
        {
            continue;
        }
        if (Applied < MaxAppliesPerFrame && State.Job->IsReady())
        {
            Apply(It.Key(), State.Job->Consume());
            State.Built = State.Pending;
            State.Job.Reset();
            ++Applied;
        }
        else
        {
            ++InFlight;
        }
    }

    if (RingOrder.IsEmpty())
    {
        for (int32 Y = -RadiusTiles; Y <= RadiusTiles; ++Y)
        {
            for (int32 X = -RadiusTiles; X <= RadiusTiles; ++X)
            {
                RingOrder.Emplace(X, Y);
            }
        }
        RingOrder.Sort([](FIntPoint A, FIntPoint B) { return A.SizeSquared() < B.SizeSquared(); });
    }

    for (const FIntPoint Offset : RingOrder)
    {
        if (InFlight >= MaxJobs)
        {
            break;
        }
        const FIntPoint Tile = CenterTile + Offset;
        FSignature Want;
        Want.Step = StepFor(RingOf(Offset));
        Want.Hole = Clip(Near, FIntRect(Tile * TileCells, (Tile + FIntPoint(1)) * TileCells));

        FTileState& State = Tiles.FindOrAdd(Tile);
        if (State.Job.IsSet() || State.Built == Want)
        {
            continue;
        }
        State.Pending = Want;
        State.Job = Async(EAsyncExecution::ThreadPool, [Seed, Tile, Want]
        {
            return BuildTile(Seed, Tile, Want.Step, Want.Hole);
        });
        ++InFlight;
    }
}

void AAethraHorizon::Apply(FIntPoint Tile, FMeshData&& Data)
{
    CSV_SCOPED_TIMING_STAT(Aethra, HorizonUpload);
    TObjectPtr<UProceduralMeshComponent>& Mesh = Meshes.FindOrAdd(Tile);
    if (!Mesh)
    {
        Mesh = NewObject<UProceduralMeshComponent>(this);
        Mesh->SetupAttachment(GetRootComponent());
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetMaterial(0, Material);
        Mesh->RegisterComponent();
    }
    Mesh->CreateMeshSection_LinearColor(0, Data.Vertices, Data.Triangles, Data.Normals, TArray<FVector2D>(),
        Data.Colors, TArray<FProcMeshTangent>(), false);
}
