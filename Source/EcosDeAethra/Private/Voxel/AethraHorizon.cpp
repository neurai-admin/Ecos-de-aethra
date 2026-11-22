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

    TArray<int32> Heights, Grounds; // Grounds: the top without canopy (block world smoothing)
    TArray<EAethraBlock> Tops, GroundTops;
    Heights.SetNumUninitialized(Row * Row);
    Tops.SetNumUninitialized(Row * Row);
    Grounds.SetNumUninitialized(Row * Row);
    GroundTops.SetNumUninitialized(Row * Row);
    TStaticArray<EAethraBlock, Height> Column;
    const bool bBlockWorld = FAethraBlockGen::IsAethraSeed(Seed);
    for (int32 J = -1; J <= Count; ++J)
    {
        for (int32 I = -1; I <= Count; ++I)
        {
            GenerateColumn(Base + FIntPoint(I, J) * Step, Seed, Column);
            int32 Top = Height - 1;
            // Block world: forests keep their canopy height (a forest mass on the far slopes); bare trunks do not.
            bool bCanopy = false;
            while (Top >= 0 && (!IsSurface(Column[Top]) // plants are too small for the horizon
                || (bBlockWorld && (Column[Top] == EAethraBlock::Wood || Column[Top] == EAethraBlock::BirchWood))))
            {
                bCanopy |= IsLeaves(Column[Top]);
                --Top;
            }
            const int32 Slot = (J + 1) * Row + I + 1;
            Heights[Slot] = Top + 1; // 0 = no ground (outside the world)
            Tops[Slot] = Top >= 0 ? (bCanopy ? EAethraBlock::Leaves : Column[Top]) : EAethraBlock::Air;
            int32 Ground = Top;
            while (bBlockWorld && Ground >= 0 && (IsLeaves(Column[Ground]) || !IsSurface(Column[Ground])
                || Column[Ground] == EAethraBlock::Wood || Column[Ground] == EAethraBlock::BirchWood))
            {
                --Ground;
            }
            Grounds[Slot] = Ground + 1;
            GroundTops[Slot] = Ground >= 0 ? Column[Ground] : EAethraBlock::Air;
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

    if (bBlockWorld)
    {
        // Block world: the far land is one smooth surface (no boxes): every corner takes the mean ground
        // height and colour of the samples around it, with smooth normals. Forests are a blanket on top:
        // the mean canopy height in a 4 x 4 window times the share of it that is forest, so dense woods
        // rise as a rolling mass and lone trees barely show (no spikes).
        const int32 CRow = Count + 1;
        TArray<float> CH;
        TArray<FLinearColor> CC;
        CH.SetNumZeroed(CRow * CRow);
        CC.SetNumZeroed(CRow * CRow);
        for (int32 J = 0; J <= Count; ++J)
        {
            for (int32 I = 0; I <= Count; ++I)
            {
                float Sum = 0.f;
                FLinearColor Col(0, 0, 0, 0);
                int32 N = 0;
                for (int32 D = 0; D < 4; ++D)
                {
                    const int32 SI = I - 1 + (D & 1), SJ = J - 1 + (D >> 1);
                    const int32 Slot = (SJ + 1) * Row + SI + 1;
                    if (Grounds[Slot] > 0)
                    {
                        Sum += Grounds[Slot];
                        ++N;
                    }
                }
                float Canopy = 0.f, Forest = 0.f;
                int32 Window = 0;
                for (int32 SJ = J - 2; SJ <= J + 1; ++SJ)
                {
                    for (int32 SI = I - 2; SI <= I + 1; ++SI)
                    {
                        if (SI < -1 || SJ < -1 || SI > Count || SJ > Count)
                        {
                            continue;
                        }
                        const int32 Slot = (SJ + 1) * Row + SI + 1;
                        ++Window;
                        if (Tops[Slot] != GroundTops[Slot] && IsLeaves(Tops[Slot]))
                        {
                            Canopy += Heights[Slot] - Grounds[Slot];
                            Forest += 1.f;
                            Col += AethraBlockColor(Tops[Slot], FIntVector(Base.X + SI * Step, Base.Y + SJ * Step, 0), FIntVector(0, 0, 1)) * .62f; // canopy in shade
                        }
                        else if (Grounds[Slot] > 0)
                        {
                            Col += AethraBlockColor(GroundTops[Slot], FIntVector(Base.X + SI * Step, Base.Y + SJ * Step, 0), FIntVector(0, 0, 1));
                        }
                    }
                }
                const float Share = Window ? Forest / Window : 0.f;
                CH[J * CRow + I] = N ? Sum / N + (Forest > 0.f ? Canopy / Forest * FMath::SmoothStep(.1f, .5f, Share) : 0.f) : 0.f;
                CC[J * CRow + I] = Window ? Col / static_cast<float>(Window) : FLinearColor::Black;
            }
        }
        const auto CornerAt = [&](int32 I, int32 J)
        {
            return FVector((Base.X + I * Step) * CellSizeCm, (Base.Y + J * Step) * CellSizeCm, CH[J * CRow + I] * CellSizeCm);
        };
        const auto NormalAt = [&](int32 I, int32 J)
        {
            const float L = CH[J * CRow + FMath::Max(I - 1, 0)], R = CH[J * CRow + FMath::Min(I + 1, Count)];
            const float Dn = CH[FMath::Max(J - 1, 0) * CRow + I], Up = CH[FMath::Min(J + 1, Count) * CRow + I];
            return FVector(-(R - L), -(Up - Dn), 2.0 * Step).GetSafeNormal();
        };
        const auto AddVertex = [&](int32 I, int32 J, float Id)
        {
            Mesh.Vertices.Add(CornerAt(I, J));
            Mesh.Normals.Add(NormalAt(I, J));
            FLinearColor C = CC[J * CRow + I];
            C.A = Id; // the cell's block id for the material (constant across the quad)
            Mesh.Colors.Add(C);
        };
        for (int32 J = 0; J < Count; ++J)
        {
            for (int32 I = 0; I < Count; ++I)
            {
                const FIntPoint Cell = Base + FIntPoint(I, J) * Step;
                const int32 Slot = (J + 1) * Row + I + 1;
                if (Grounds[Slot] == 0 || InHole(Cell))
                {
                    continue;
                }
                const float Id = static_cast<uint8>(Tops[Slot] != GroundTops[Slot] && IsLeaves(Tops[Slot]) ? Tops[Slot] : GroundTops[Slot]) / 31.0f;
                // Same vertex order and winding as the box tops: (x0,y0), (x0,y1), (x1,y1), (x1,y0).
                const int32 Start = Mesh.Vertices.Num();
                AddVertex(I, J, Id);
                AddVertex(I, J + 1, Id);
                AddVertex(I + 1, J + 1, Id);
                AddVertex(I + 1, J, Id);
                Mesh.Triangles.Append({ Start, Start + 1, Start + 2, Start, Start + 2, Start + 3 });

                // Skirts hide cracks: down to bedrock at the voxel hole, a few cells at the tile edge.
                for (const FIntPoint Side : Sides)
                {
                    const int32 NI = I + Side.X, NJ = J + Side.Y;
                    const bool bHole = InHole(Cell + Side * Step);
                    if (!bHole && NI >= 0 && NI < Count && NJ >= 0 && NJ < Count)
                    {
                        continue;
                    }
                    // The edge's corners, ordered so the skirt faces outward.
                    FIntPoint A, B;
                    if (Side.X > 0)      { A = { I + 1, J }; B = { I + 1, J + 1 }; }
                    else if (Side.X < 0) { A = { I, J + 1 }; B = { I, J }; }
                    else if (Side.Y > 0) { A = { I + 1, J + 1 }; B = { I, J + 1 }; }
                    else                 { A = { I, J }; B = { I + 1, J }; }
                    const FVector TA = CornerAt(A.X, A.Y), TB = CornerAt(B.X, B.Y);
                    const double Drop = bHole ? 0.0 : FMath::Min(TA.Z, TB.Z) - TileSkirtCells * CellSizeCm;
                    const FVector BA(TA.X, TA.Y, FMath::Max(Drop, 0.0)), BB(TB.X, TB.Y, FMath::Max(Drop, 0.0));
                    // Painted and lit like the land above it, so a skirt seen through a seam blends in.
                    FLinearColor C = (CC[A.Y * CRow + A.X] + CC[B.Y * CRow + B.X]) * .5f;
                    C.A = Id;
                    const int32 S = Mesh.Vertices.Num();
                    Mesh.Vertices.Append({ BA, TA, TB, BB }); // same winding as the box sides
                    Mesh.Triangles.Append({ S, S + 1, S + 2, S, S + 2, S + 3 });
                    for (int32 V = 0; V < 4; ++V)
                    {
                        Mesh.Normals.Add(FVector::UpVector);
                        Mesh.Colors.Add(C);
                    }
                }
            }
        }
        return Mesh;
    }
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
