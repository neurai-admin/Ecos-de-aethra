#include "VisualWorld/AethraBlockGen.h"

#include "Voxel/AethraTerrain.h"
#include "VisualWorld/AethraCaveGen.h"
#include "VisualWorld/AethraHybridGeometry.h"
#include "VisualWorld/AethraRuinGen.h"

static_assert(FAethraBlockGen::ColumnHeight == AethraTerrain::Height, "the block world fills voxel columns");

// Names prefixed (Block...) because the unity build merges this file with others.
namespace
{
using EB = EAethraBlock;

uint32 BlockHash(int32 X, int32 Y, int32 Seed, uint32 Salt)
{
    uint32 V = static_cast<uint32>(X) * 0x9E3779B1u ^ static_cast<uint32>(Y) * 0x85EBCA77u ^ static_cast<uint32>(Seed) * 0xC2B2AE3Du ^ Salt;
    V ^= V >> 16;
    V *= 0x7FEB352Du;
    V ^= V >> 15;
    V *= 0x846CA68Bu;
    return V ^ (V >> 16);
}

EB BlockForSurface(EAmeSurface S)
{
    switch (S)
    {
    case EAmeSurface::Dirt: return EB::Dirt;
    case EAmeSurface::Mud: return EB::Mud;
    case EAmeSurface::Sand: return EB::Sand;
    case EAmeSurface::Gravel: return EB::Gravel;
    case EAmeSurface::Snow: return EB::Snow;
    case EAmeSurface::Granite: return EB::Stone;
    case EAmeSurface::Basalt: return EB::Basalt;
    case EAmeSurface::Limestone: return EB::Limestone;
    case EAmeSurface::Sandstone: return EB::Sandstone;
    case EAmeSurface::Slate: return EB::Slate;
    case EAmeSurface::Marble: return EB::Marble;
    case EAmeSurface::CutStone: case EAmeSurface::Mortar: case EAmeSurface::Brick: return EB::StoneBricks;
    case EAmeSurface::WoodPlank: case EAmeSurface::BarkOak: return EB::Planks;
    case EAmeSurface::RoofTile: return EB::RoofTiles;
    case EAmeSurface::Ceramic: return EB::Ceramic;
    case EAmeSurface::Iron: return EB::Basalt;
    case EAmeSurface::Bronze: return EB::Sandstone;
    case EAmeSurface::Plaster: return EB::Limestone;
    case EAmeSurface::Aetherite: case EAmeSurface::Crystal: return EB::Aetherite;
    case EAmeSurface::OreCopper: case EAmeSurface::OreIron: return EB::Stone;
    default: return EB::Grass; // grass, meadow, moss
    }
}

bool BlockIsRock(EB B)
{
    return B == EB::Stone || (B >= EB::Basalt && B <= EB::Marble);
}

// Point inside a unit AHG shape (x, y in [-0.5, 0.5], z in [0, 1]); blocky approximations.
bool BlockInsideShape(EAhgShape Shape, const FVector& U)
{
    if (FMath::Abs(U.X) > .5 || FMath::Abs(U.Y) > .5 || U.Z < 0.0 || U.Z > 1.0)
    {
        return false;
    }
    const double R2 = U.X * U.X + U.Y * U.Y;
    switch (Shape)
    {
    case EAhgShape::HalfBlock: return U.Z <= .5;
    case EAhgShape::Wedge: case EAhgShape::Ramp: return U.Z <= .5 - U.X + .05;
    case EAhgShape::Stairs: return U.Z <= FMath::CeilToDouble((.5 - U.X) * 4.0) / 4.0;
    case EAhgShape::Cylinder: case EAhgShape::Column: return R2 <= .25;
    case EAhgShape::Sphere: return R2 + (U.Z - .5) * (U.Z - .5) <= .25;
    case EAhgShape::Dome: return R2 + U.Z * U.Z * .25 <= .25;
    case EAhgShape::ConeRoof: return FMath::Sqrt(R2) <= .5 * (1.0 - U.Z);
    case EAhgShape::GableRoof: return FMath::Abs(U.X) <= .5 * (1.0 - U.Z);
    case EAhgShape::Arch: return !(FMath::Abs(U.X) < .3 && U.Z < .7);
    default: return true; // Block, CurvedWall
    }
}

// The blocks of a tree at (dx, dy, dz) from the trunk base (dz = 1 is the first block above the
// ground). Common trees are boxes and steps, pines stepped diamonds, the ancient tree a big rounded mass.
EB BlockTree(const FFloraTree& T, int32 DX, int32 DY, int32 DZ)
{
    const uint32 H = static_cast<uint32>(T.Id ^ (T.Id >> 32));
    const int32 Extra = FMath::Clamp(FMath::RoundToInt((T.Scale - 1.f) * 4.f), -1, 2) + T.Variant % 2;
    const int32 AX = FMath::Abs(DX), AY = FMath::Abs(DY), Cheb = FMath::Max(AX, AY);
    const int32 D2 = DX * DX + DY * DY; // crowns are round (discs and domes), not boxes
    switch (T.Species)
    {
    case EFloraSpecies::Oak:
    {
        const int32 Trunk = 4 + Extra;
        if (Cheb == 0 && DZ <= Trunk)
        {
            return EB::Wood;
        }
        const int32 L = DZ - Trunk + 1; // crown layers 0..3: a dome
        const int32 Radius2[] = { 5, 8, 5, 2 };
        return L >= 0 && L < 4 && D2 <= Radius2[L] ? EB::Leaves : EB::Air;
    }
    case EFloraSpecies::Birch:
    {
        const int32 Trunk = 5 + Extra;
        if (Cheb == 0 && DZ <= Trunk)
        {
            return EB::BirchWood;
        }
        const int32 L = DZ - Trunk + 2;
        const int32 Radius2[] = { 2, 2, 1, 0 };
        return L >= 0 && L < 4 && D2 <= Radius2[L] ? EB::BirchLeaves : EB::Air;
    }
    case EFloraSpecies::Pine:
    {
        const int32 Trunk = 7 + Extra;
        if (Cheb == 0 && DZ <= Trunk)
        {
            return EB::Wood;
        }
        static constexpr int32 Crown[] = { 2, 1, 2, 1, 1, 0, 0 };
        const int32 L = DZ - 3;
        return L >= 0 && L < 7 && AX + AY <= Crown[L] && DZ <= Trunk + 2 ? EB::PineLeaves : EB::Air;
    }
    case EFloraSpecies::Bush:
        return (DZ == 1 && D2 <= 1) || (DZ == 2 && Cheb == 0) ? EB::Leaves : EB::Air;
    case EFloraSpecies::Willow:
    {
        const int32 Trunk = 4 + Extra;
        if (Cheb == 0 && DZ <= Trunk)
        {
            return EB::Wood;
        }
        if ((DZ == Trunk + 1 && D2 <= 10) || (DZ == Trunk + 2 && D2 <= 5))
        {
            return EB::BirchLeaves;
        }
        // Hanging curtains along the canopy edge.
        return D2 >= 7 && D2 <= 10 && DZ >= Trunk - 2 && DZ <= Trunk && (BlockHash(DX, DY, static_cast<int32>(H), 7u) & 3) != 0 ? EB::BirchLeaves : EB::Air;
    }
    case EFloraSpecies::Palm:
    {
        const int32 Trunk = 6 + Extra;
        const int32 Lean = DZ / 3; // the trunk steps sideways as it rises
        if (DX == Lean && DY == 0 && DZ <= Trunk)
        {
            return EB::Wood;
        }
        const int32 TX = Trunk / 3;
        const int32 FX = DX - TX;
        const bool bFrond = (DY == 0 && FMath::Abs(FX) <= 3) || (FX == 0 && FMath::Abs(DY) <= 3);
        const int32 Reach = FMath::Max(FMath::Abs(FX), FMath::Abs(DY));
        return bFrond && DZ == Trunk + 1 - (Reach == 3 ? 1 : 0) ? EB::Leaves : EB::Air;
    }
    case EFloraSpecies::DeadTree:
    {
        const int32 Trunk = 4 + Extra;
        if (Cheb == 0 && DZ <= Trunk)
        {
            return EB::Wood;
        }
        const int32 Dir = H & 3;
        const FIntPoint Arm[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int32 K = 0; K < 2; ++K)
        {
            const FIntPoint A = Arm[(Dir + K * 2) % 4];
            const int32 Z = Trunk - 1 + K;
            for (int32 S = 1; S <= 2; ++S)
            {
                if (DX == A.X * S && DY == A.Y * S && DZ == Z + S - 1)
                {
                    return EB::Wood;
                }
            }
        }
        return EB::Air;
    }
    case EFloraSpecies::Ancient:
    {
        const int32 Trunk = 8 + Extra;
        if (DX >= 0 && DX <= 1 && DY >= 0 && DY <= 1 && DZ <= Trunk)
        {
            return EB::Wood;
        }
        const float CX = DX - .5f, CY = DY - .5f, CZ = (DZ - Trunk - 1) * 1.4f;
        return CX * CX + CY * CY + CZ * CZ <= 19.f && DZ > Trunk - 3 ? EB::Leaves : EB::Air;
    }
    default:
        return EB::Air;
    }
}

constexpr int32 BlockCrownReach = 4; // the widest tree reaches 4 blocks from its trunk
}

// ---------------------------------------------------------------- registry

const FAethraBlockGen& FAethraBlockGen::Get(int32 Seed)
{
    static FCriticalSection Lock;
    static TMap<int32, TUniquePtr<FAethraBlockGen>> Generators;
    FScopeLock Guard(&Lock);
    TUniquePtr<FAethraBlockGen>& G = Generators.FindOrAdd(Seed);
    if (!G)
    {
        G = MakeUnique<FAethraBlockGen>(WorldSeedOf(Seed));
    }
    return *G;
}

float FAethraBlockGen::MapHeight(float M)
{
    float F;
    if (M < 0.f)
    {
        F = FMath::Max(M * .6f, -9.f);
    }
    else if (M < 40.f)
    {
        F = M;
    }
    else if (M < 200.f)
    {
        F = 40.f + (M - 40.f) * .28f;
    }
    else
    {
        F = 84.8f + (M - 200.f) * .045f;
    }
    return SeaLevel + F;
}

FAethraBlockGen::FAethraBlockGen(int32 WorldSeed)
{
    Gen = MakeUnique<FAethraWorldGen>(WorldSeed);
    CaveGen = MakeUnique<FAethraCaveGen>(*Gen, FVector2D::ZeroVector);
    RuinGen = MakeUnique<FAethraRuinGen>(*Gen, CaveGen.Get(), FVector2D::ZeroVector);
    for (const FRuinSite& Site : RuinGen->Sites())
    {
        Rasterise(Site);
    }
    // Cave formations: stalactites and stalagmites of limestone, Aetherite crystals.
    TArray<FAethraCaveGen::FFormation> Formations;
    for (const FCaveSystem& Sys : CaveGen->Systems())
    {
        Formations.Reset();
        CaveGen->Formations(Sys, Formations);
        for (const FAethraCaveGen::FFormation& F : Formations)
        {
            const FIntPoint XY(FMath::FloorToInt(F.Base.X), FMath::FloorToInt(F.Base.Y));
            const FColumnInfo C = Column(XY);
            const int32 BaseZ = FMath::FloorToInt(C.Ground - (C.Sample.Height - F.Base.Z));
            const int32 Length = FMath::Clamp(FMath::CeilToInt(F.Size), 1, 3);
            for (int32 K = 0; K < Length; ++K)
            {
                const FVector P = FVector(XY.X, XY.Y, BaseZ) + F.Up * K;
                AddCell(FIntVector(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y), FMath::FloorToInt(P.Z)), F.Type == 2 ? EB::Aetherite : EB::Limestone);
            }
        }
    }
    // Spawn: on a spiral around the origin, the first dry, gentle, open column within sight of water
    // (the river near the demo spawn); else the first dry gentle one.
    TOptional<FIntPoint> Dry;
    TStaticArray<EAethraBlock, ColumnHeight> Probe;
    bool bFound = false;
    for (int32 R = 0; R < 400 && !bFound; R += 4)
    {
        for (int32 K = 0; K < 16; ++K)
        {
            const FIntPoint P(FMath::RoundToInt(FMath::Cos(K * PI / 8.f) * R), FMath::RoundToInt(FMath::Sin(K * PI / 8.f) * R));
            const FColumnInfo C = Column(P);
            if (C.WaterTop >= 0 || C.Sample.WaterDistance <= 6.f || FMath::Abs(Column(P + FIntPoint(1, 0)).Ground - C.Ground) > 1 || RuinColumns.Contains(P))
            {
                continue;
            }
            if (!Dry)
            {
                Dry = P;
            }
            GenerateColumn(P, Probe);
            bool bOpen = true; // no trunk or leaves over the spawn
            for (int32 Z = C.Ground + 1; Z < FMath::Min(C.Ground + 6, ColumnHeight); ++Z)
            {
                bOpen &= Probe[Z] == EB::Air || AethraTerrain::IsPlant(Probe[Z]);
            }
            if (bOpen && C.Sample.WaterDistance < 24.f && AethraTerrain::IsOpaque(Probe[C.Ground]))
            {
                Spawn = P;
                bFound = true;
                break;
            }
        }
    }
    if (!bFound && Dry)
    {
        Spawn = *Dry;
    }
    for (int32 R = 1; R <= 32; ++R)
    {
        int32 Best = -1;
        for (int32 K = 0; K < 32 && Best < 0; ++K)
        {
            const float A = K * 2.f * PI / 32.f;
            if (Column(Spawn + FIntPoint(FMath::RoundToInt(FMath::Cos(A) * R), FMath::RoundToInt(FMath::Sin(A) * R))).WaterTop >= 0)
            {
                Best = K;
            }
        }
        if (Best >= 0)
        {
            SpawnFacing = Best * 360.f / 32.f;
            break;
        }
    }
}

FAethraBlockGen::~FAethraBlockGen() = default;

void FAethraBlockGen::AddCell(const FIntVector& Cell, EAethraBlock Block)
{
    if (Cell.Z > 0 && Cell.Z < ColumnHeight)
    {
        RuinColumns.FindOrAdd(FIntPoint(Cell.X, Cell.Y)).Add({ static_cast<int16>(Cell.Z), Block });
    }
}

void FAethraBlockGen::Rasterise(const FRuinSite& Site)
{
    TArray<FRuinPiece> Pieces;
    FAethraRuinGen::Build(Site, Gen->GetSeed(), Pieces);
    const FIntPoint AnchorXY(FMath::FloorToInt(Site.Anchor.X), FMath::FloorToInt(Site.Anchor.Y));
    const FColumnInfo A = Column(AnchorXY);
    // Surface ruins start on top of the ground; underground ones at the cave floor (1:1 below ground).
    const int32 Base = Site.bUnderground ? FMath::FloorToInt(A.Ground - (A.Sample.Height - Site.Anchor.Z)) : A.Ground + 1;
    for (const FRuinPiece& P : Pieces)
    {
        const EB Block = BlockForSurface(P.Surface);
        if (P.Surface == EAmeSurface::Moss)
        {
            continue; // thin moss slabs are finer than a block
        }
        const FBox Box = FBox(FVector(-50, -50, 0), FVector(50, 50, 100)).TransformBy(P.Transform);
        const FIntVector Lo(FMath::FloorToInt(Box.Min.X / 100.0), FMath::FloorToInt(Box.Min.Y / 100.0), FMath::FloorToInt(Box.Min.Z / 100.0));
        const FIntVector Hi(FMath::FloorToInt(Box.Max.X / 100.0), FMath::FloorToInt(Box.Max.Y / 100.0), FMath::FloorToInt(Box.Max.Z / 100.0));
        for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
        {
            for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
            {
                for (int32 X = Lo.X; X <= Hi.X; ++X)
                {
                    const FVector Centre = FVector(X + .5, Y + .5, Z + .5) * 100.0;
                    const FVector U = P.Transform.InverseTransformPosition(Centre) / 100.0;
                    if (BlockInsideShape(P.Shape, U))
                    {
                        AddCell(FIntVector(X, Y, Base + Z - FMath::FloorToInt(Site.Anchor.Z)), Block);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------- columns

FAethraBlockGen::FColumnInfo FAethraBlockGen::ComputeColumn(FIntPoint P) const
{
    FColumnInfo C;
    const FVector2D M(P.X + .5, P.Y + .5);
    C.Sample = Gen->Sample(M);
    const FWorldSample& S = C.Sample;
    const float GX = Gen->Height(M + FVector2D(1, 0)) - S.Height, GY = Gen->Height(M + FVector2D(0, 1)) - S.Height;
    const FVector3f Normal = FVector3f(-GX, -GY, 1.f).GetSafeNormal();
    FAmeEnvironment Env;
    Env.Biome = S.Biome;
    Env.Rock = S.Rock;
    Env.Altitude = S.Height;
    Env.Moisture = S.Moisture;
    Env.Temperature = S.Temperature;
    Env.Slope = 1.f - Normal.Z;
    Env.WaterDistance = S.WaterDistance;
    const FAmeChoice Choice = FAmeSelector::Terrain(Env, Gen->GetSeed(), P);
    C.Surface = BlockForSurface(Choice.Blend > .5f ? Choice.Secondary : Choice.Primary);
    C.Rock = BlockForSurface(FAmeSelector::RockSurface(S.Rock));
    C.Subsoil = BlockIsRock(C.Surface) ? C.Surface : C.Surface == EB::Sand || C.Surface == EB::Gravel ? C.Surface : EB::Dirt;
    C.Ground = FMath::Clamp(FMath::FloorToInt(MapHeight(S.Height)), 2, ColumnHeight - 8);
    // Near water, the ground stays below the continuous water surface, so the smooth sheet meets the
    // banks without a stepped shoreline.
    const float Shore = S.ShoreLevel > -1e8f ? S.ShoreLevel : S.IsWater() ? S.WaterLevel : -1e9f;
    if (Shore > -1e8f)
    {
        C.Plane = MapHeight(Shore);
        // The bank rises one block per 1.5 m from the water, instead of one wall at the edge of the band.
        const int32 Rise = S.IsWater() ? 0 : FMath::Max(0, FMath::FloorToInt((S.WaterDistance - .8f) / 1.5f));
        C.Ground = FMath::Max(2, FMath::Min(C.Ground, FMath::FloorToInt(C.Plane) - 1 + Rise));
        if (!S.IsWater() && (C.Surface == EB::Dirt || C.Surface == EB::Mud))
        {
            C.Surface = EB::Grass; // green banks down to the water
        }
    }
    if (S.IsWater())
    {
        C.WaterTop = FMath::Clamp(FMath::FloorToInt(C.Plane) - 1, 3, ColumnHeight - 8);
        C.Ground = FMath::Min(C.Ground, C.WaterTop - 1);
        // Beds: sand in the sea, gravel in rivers and streams, mud in lowland lakes.
        const EHydroKind Kind = Gen->GetBodies().IsValidIndex(S.WaterBody) ? Gen->GetBodies()[S.WaterBody].Kind : EHydroKind::River;
        C.Surface = Kind == EHydroKind::Sea ? EB::Sand : Kind == EHydroKind::Lake ? EB::Mud : EB::Gravel;
        C.Subsoil = C.Surface == EB::Mud ? EB::Dirt : C.Surface;
    }
    return C;
}

FAethraBlockGen::FColumnInfo FAethraBlockGen::Column(FIntPoint P) const
{
    {
        FReadScopeLock Read(CacheLock);
        if (const FColumnInfo* Found = ColumnCache.Find(P))
        {
            return *Found;
        }
    }
    const FColumnInfo C = ComputeColumn(P);
    FWriteScopeLock Write(CacheLock);
    if (ColumnCache.Num() > 300000)
    {
        ColumnCache.Reset(); // shortcut: a flat cache; a region cache would keep the neighbours of streaming chunks
    }
    ColumnCache.Add(P, C);
    return C;
}

TOptional<FFloraTree> FAethraBlockGen::Tree(FIntPoint Cell) const
{
    {
        FReadScopeLock Read(CacheLock);
        if (const TOptional<FFloraTree>* Found = TreeCache.Find(Cell))
        {
            return *Found;
        }
    }
    TOptional<FFloraTree> Result;
    FFloraTree T;
    if (FFloraPlacer::TreeAt(*Gen, Cell, T) && !RuinGen->SurfaceSiteAt(T.Position) && !CaveGen->NearEntrance(T.Position, 6.0))
    {
        Result = T;
    }
    FWriteScopeLock Write(CacheLock);
    if (TreeCache.Num() > 100000)
    {
        TreeCache.Reset();
    }
    TreeCache.Add(Cell, Result);
    return Result;
}

void FAethraBlockGen::BuildWaterSurface(FIntPoint O, TArray<FVector>& Vertices, TArray<FVector>& Normals,
    TArray<int32>& Triangles, TArray<FVector2D>& UV, TArray<FLinearColor>& Colors) const
{
    constexpr int32 N = AethraTerrain::ChunkSize;
    // Cells -1..N around the chunk; corners average their four cells, so neighbouring chunks share edges.
    struct FCell { float Plane; float Alpha; FVector2f Flow; };
    FCell Cells[N + 2][N + 2];
    bool bAny = false;
    for (int32 Y = -1; Y <= N; ++Y)
    {
        for (int32 X = -1; X <= N; ++X)
        {
            const FColumnInfo C = Column(O + FIntPoint(X, Y));
            FCell& Out = Cells[Y + 1][X + 1];
            Out.Plane = C.Plane;
            Out.Alpha = C.WaterTop >= 0 ? 1.f : C.Plane > -1e8f ? FMath::Clamp(1.f - C.Sample.WaterDistance / 1.2f, 0.f, 1.f) : 0.f;
            Out.Flow = C.Sample.Flow;
            bAny |= Out.Alpha > 0.f && X >= 0 && Y >= 0 && X < N && Y < N;
        }
    }
    if (!bAny)
    {
        return;
    }
    struct FCorner { float Plane = 0.f; float Alpha = 0.f; FVector2f Flow = FVector2f::ZeroVector; bool bValid = false; };
    FCorner Corners[N + 1][N + 1];
    for (int32 J = 0; J <= N; ++J)
    {
        for (int32 I = 0; I <= N; ++I)
        {
            FCorner& K = Corners[J][I];
            int32 Count = 0;
            for (int32 D = 0; D < 4; ++D)
            {
                const FCell& C = Cells[J + (D >> 1)][I + (D & 1)];
                K.Alpha += C.Alpha * .25f;
                if (C.Plane > -1e8f)
                {
                    K.Plane += C.Plane;
                    K.Flow += C.Flow;
                    ++Count;
                }
            }
            K.bValid = Count > 0;
            if (Count)
            {
                K.Plane /= Count;
                K.Flow /= static_cast<float>(Count);
            }
        }
    }
    for (int32 J = 0; J < N; ++J)
    {
        for (int32 I = 0; I < N; ++I)
        {
            const FCorner* Q[4] = { &Corners[J][I], &Corners[J][I + 1], &Corners[J + 1][I + 1], &Corners[J + 1][I] };
            float Level = 0.f, MaxAlpha = 0.f;
            int32 Valid = 0;
            for (const FCorner* K : Q)
            {
                MaxAlpha = FMath::Max(MaxAlpha, K->Alpha);
                if (K->bValid)
                {
                    Level += K->Plane;
                    ++Valid;
                }
            }
            if (MaxAlpha < .01f || Valid == 0)
            {
                continue;
            }
            Level /= Valid;
            const int32 Start = Vertices.Num();
            const FIntPoint At[4] = { { I, J }, { I + 1, J }, { I + 1, J + 1 }, { I, J + 1 } };
            for (int32 V = 0; V < 4; ++V)
            {
                const FCorner& K = *Q[V];
                const float Z = (K.bValid ? K.Plane : Level) * 100.f - 6.f; // just under the true level
                Vertices.Add(FVector(At[V].X * 100.0, At[V].Y * 100.0, Z));
                Normals.Add(FVector::UpVector);
                UV.Add(FVector2D(At[V].X, At[V].Y));
                const float Speed = K.Flow.Size();
                const FVector2f Dir = Speed > 1e-3f ? K.Flow / Speed : FVector2f::ZeroVector;
                Colors.Add(FLinearColor(.5f + .5f * Dir.X, .5f + .5f * Dir.Y, .5f + .5f * FMath::Min(Speed / 1.5f, 1.f), K.Alpha));
            }
            Triangles.Append({ Start, Start + 2, Start + 1, Start, Start + 3, Start + 2 });
        }
    }
}

int32 FAethraBlockGen::SurfaceAt(FIntPoint Point) const
{
    return Column(Point).Ground;
}

void FAethraBlockGen::GenerateColumn(FIntPoint P, TStaticArray<EAethraBlock, ColumnHeight>& Out) const
{
    for (EAethraBlock& B : Out)
    {
        B = EB::Air;
    }
    if (!AethraTerrain::Contains(FIntVector(P.X, P.Y, 0)))
    {
        return;
    }
    const FColumnInfo C = Column(P);
    const FWorldSample& S = C.Sample;
    for (int32 Z = 0; Z <= C.Ground; ++Z)
    {
        Out[Z] = Z == 0 || Z < C.Ground - 3 ? C.Rock : Z < C.Ground ? C.Subsoil : C.Surface;
    }
    for (int32 Z = C.Ground + 1; Z <= C.WaterTop; ++Z)
    {
        Out[Z] = EB::Water;
    }

    // Caves: carved from the rock below the ground, 1 m of depth per block.
    const FVector2D M(P.X + .5, P.Y + .5);
    for (const FCaveSystem& Sys : CaveGen->Systems())
    {
        const FBox& B = Sys.Bounds;
        if (M.X < B.Min.X || M.X > B.Max.X || M.Y < B.Min.Y || M.Y > B.Max.Y)
        {
            continue;
        }
        const int32 Lo = FMath::Max(1, FMath::FloorToInt(C.Ground - (S.Height - B.Min.Z)));
        const int32 Hi = FMath::Min(C.Ground, FMath::CeilToInt(C.Ground - (S.Height - B.Max.Z)));
        for (int32 Z = Lo; Z <= Hi; ++Z)
        {
            const float ZM = S.Height - (C.Ground - Z) + .5f;
            if (CaveGen->CaveDensity(FVector(M.X, M.Y, ZM)) < 0.f)
            {
                Out[Z] = ZM < Sys.PoolLevel ? EB::Water : EB::Air;
            }
        }
    }

    // Ruins and cave formations (rasterised at start).
    if (const TArray<FRuinCell>* Cells = RuinColumns.Find(P))
    {
        for (const FRuinCell& R : *Cells)
        {
            Out[R.Z] = R.Block;
        }
    }

    // Trees from FloraGen, as block shapes per species; only into air (never into ground or ruins).
    const int32 X0 = AethraTerrain::FloorDiv(P.X - BlockCrownReach, 7), X1 = AethraTerrain::FloorDiv(P.X + BlockCrownReach, 7);
    const int32 Y0 = AethraTerrain::FloorDiv(P.Y - BlockCrownReach, 7), Y1 = AethraTerrain::FloorDiv(P.Y + BlockCrownReach, 7);
    bool bUnderCrown = false;
    for (int32 CY = Y0; CY <= Y1; ++CY)
    {
        for (int32 CX = X0; CX <= X1; ++CX)
        {
            const TOptional<FFloraTree> T = Tree(FIntPoint(CX, CY));
            if (!T)
            {
                continue;
            }
            const FIntPoint Trunk(FMath::FloorToInt(T->Position.X), FMath::FloorToInt(T->Position.Y));
            const int32 DX = P.X - Trunk.X, DY = P.Y - Trunk.Y;
            if (FMath::Abs(DX) > BlockCrownReach || FMath::Abs(DY) > BlockCrownReach)
            {
                continue;
            }
            const FColumnInfo Base = DX == 0 && DY == 0 ? C : Column(Trunk);
            if (Base.WaterTop >= 0)
            {
                continue;
            }
            for (int32 DZ = 1; DZ <= 16 && Base.Ground + DZ < ColumnHeight; ++DZ)
            {
                const EB Block = BlockTree(*T, DX, DY, DZ);
                const int32 Z = Base.Ground + DZ;
                if (Block != EB::Air && Out[Z] == EB::Air)
                {
                    Out[Z] = Block;
                    bUnderCrown = true;
                }
            }
        }
    }

    // Ground flora: flower fields, ferns, grass and wild flowers on open grass.
    const int32 Top = C.Ground + 1;
    if (!bUnderCrown && C.WaterTop < 0 && Out[C.Ground] == EB::Grass && Top < ColumnHeight && Out[Top] == EB::Air)
    {
        const FFloraEnvironment Env = FFloraEnvironment::From(S, FVector3f(0, 0, 1));
        const FFieldSample Field = FFloraGen::FieldAt(Gen->GetSeed(), M, Env);
        const float Roll = (BlockHash(P.X, P.Y, Gen->GetSeed(), 0x51u) & 0xFFFF) / 65535.f;
        const float Pick = (BlockHash(P.X, P.Y, Gen->GetSeed(), 0x52u) & 0xFFFF) / 65535.f;
        EB Plant = EB::Air;
        if (Field.Strength > .2f && Roll < .15f + .55f * Field.Strength)
        {
            switch (Field.Field)
            {
            case EFlowerField::Sunflower: Plant = Pick < .85f ? EB::Sunflower : EB::TallGrass; break;
            case EFlowerField::Lavender: Plant = Pick < .85f ? EB::Lavender : EB::TallGrass; break;
            case EFlowerField::WildRose: Plant = Pick < .55f ? EB::FlowerRed : Pick < .8f ? EB::FlowerWhite : EB::TallGrass; break;
            case EFlowerField::Alpine: Plant = Pick < .75f ? EB::FlowerBlue : EB::TallGrass; break;
            default:
            {
                const float Patch = FFloraGen::Noise(Gen->GetSeed(), M, 14.0, 77u);
                Plant = Patch < .33f ? EB::FlowerWhite : Patch < .66f ? EB::FlowerRed : EB::FlowerYellow;
                break;
            }
            }
        }
        else if (S.Biome == EAmeBiome::Forest)
        {
            Plant = Roll < .16f ? EB::Fern : Roll < .3f ? EB::TallGrass : EB::Air;
        }
        else if (S.Biome == EAmeBiome::Mountain)
        {
            Plant = Roll < .12f ? EB::TallGrass : Roll < .14f ? EB::FlowerBlue : EB::Air;
        }
        else
        {
            Plant = Roll < .22f ? EB::TallGrass : Roll < .245f ? (Pick < .5f ? EB::FlowerWhite : EB::FlowerYellow) : EB::Air;
        }
        Out[Top] = Plant;
    }
}
