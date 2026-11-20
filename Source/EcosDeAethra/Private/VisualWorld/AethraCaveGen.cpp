#include "VisualWorld/AethraCaveGen.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "VisualWorld/AethraWorldGen.h"

// Names prefixed (Cave...) because the unity build merges this file with others.
namespace
{
uint32 CaveMix(uint32 H)
{
    H ^= H >> 16;
    H *= 0x7feb352dU;
    H ^= H >> 15;
    H *= 0x846ca68bU;
    H ^= H >> 16;
    return H;
}

float CaveHash(int32 Seed, int32 X, int32 Y, int32 Z, uint32 Salt)
{
    return (CaveMix(static_cast<uint32>(Seed) ^ CaveMix(static_cast<uint32>(X) * 0x9E3779B1U ^ CaveMix(static_cast<uint32>(Y) * 0x85EBCA77U ^ CaveMix(static_cast<uint32>(Z) * 0xC2B2AE3DU + Salt)))) & 0xFFFFFF) / 16777215.f;
}

// 3D value noise in [0, 1].
float CaveNoise(int32 Seed, const FVector& P)
{
    const int32 X = FMath::FloorToInt(P.X), Y = FMath::FloorToInt(P.Y), Z = FMath::FloorToInt(P.Z);
    const float FX = static_cast<float>(P.X - X), FY = static_cast<float>(P.Y - Y), FZ = static_cast<float>(P.Z - Z);
    const float SX = FX * FX * (3.f - 2.f * FX), SY = FY * FY * (3.f - 2.f * FY), SZ = FZ * FZ * (3.f - 2.f * FZ);
    float C[2][2][2];
    for (int32 K = 0; K < 8; ++K)
    {
        C[K & 1][(K >> 1) & 1][K >> 2] = CaveHash(Seed, X + (K & 1), Y + ((K >> 1) & 1), Z + (K >> 2), 5u);
    }
    auto L = [](float A, float B, float T) { return A + (B - A) * T; };
    return L(L(L(C[0][0][0], C[1][0][0], SX), L(C[0][1][0], C[1][1][0], SX), SY), L(L(C[0][0][1], C[1][0][1], SX), L(C[0][1][1], C[1][1][1], SX), SY), SZ);
}

float CaveSegment(const FVector& P, const FVector& A, const FVector& B, float& OutT)
{
    const FVector AB = B - A;
    OutT = FMath::Clamp(static_cast<float>(FVector::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1e-6)), 0.f, 1.f);
    return static_cast<float>(FVector::Dist(P, A + AB * OutT));
}

FVector2D CaveRotate(const FVector2D& V, float Degrees)
{
    const float R = FMath::DegreesToRadians(Degrees);
    return FVector2D(V.X * FMath::Cos(R) - V.Y * FMath::Sin(R), V.X * FMath::Sin(R) + V.Y * FMath::Cos(R));
}
}

FAethraCaveGen::FAethraCaveGen(const FAethraWorldGen& InGen, const FVector2D& Spawn)
    : Gen(InGen), Seed(InGen.GetSeed())
{
    // 1. The demonstration cave near the spawn: enchanted, so the first cave shows the most.
    PlanSystem(0, Spawn, ECaveKind::Enchanted, true);
    // 2. A sparse set across the domain, kind from the place: deep and dark or giant in the
    //    mountains, enchanted in some forests, natural elsewhere.
    const double Cell = 1100.0;
    int32 Id = 1;
    for (int32 Y = -3; Y <= 3 && Id < 24; ++Y)
    {
        for (int32 X = -3; X <= 3 && Id < 24; ++X)
        {
            if (CaveHash(Seed, X, Y, 0, 31u) > .45f || (X == 0 && Y == 0))
            {
                continue;
            }
            const FVector2D Near((X + .2 + .6 * CaveHash(Seed, X, Y, 1, 32u)) * Cell, (Y + .2 + .6 * CaveHash(Seed, X, Y, 2, 33u)) * Cell);
            const FWorldSample S = Gen.Sample(Near);
            const float Roll = CaveHash(Seed, X, Y, 3, 34u);
            ECaveKind Kind = ECaveKind::Natural;
            if (S.Height > 200.f)
            {
                Kind = Roll < .5f ? ECaveKind::Dark : ECaveKind::Giant;
            }
            else if (S.Biome == EAmeBiome::Forest && Roll < .35f)
            {
                Kind = ECaveKind::Enchanted;
            }
            else if (Roll > .9f)
            {
                Kind = ECaveKind::Giant;
            }
            PlanSystem(Id, Near, Kind, false);
            Id = CaveSystems.Num();
        }
    }
}

const TCHAR* FAethraCaveGen::KindName(ECaveKind Kind)
{
    static const TCHAR* Names[] = { TEXT("natural"), TEXT("encantada"), TEXT("obscura"), TEXT("gigante") };
    return Names[FMath::Clamp(static_cast<int32>(Kind), 0, 3)];
}

void FAethraCaveGen::PlanSystem(int32 Id, const FVector2D& Near, ECaveKind Kind, bool bDemo)
{
    // A hillside for the entrance: dry, away from water and settlements, moderately steep.
    FVector2D Best = FVector2D::ZeroVector, BestUp = FVector2D(1, 0);
    float BestScore = -1e9f;
    // The demo cave must exist: it searches wider and accepts gentler slopes before giving up.
    const int32 Tries = bDemo ? 240 : 64;
    for (int32 K = 0; K < Tries; ++K)
    {
        const float A = 2.f * PI * CaveHash(Seed, Id, K, 0, 41u);
        const float R = bDemo ? 140.f + (260.f + 500.f * K / Tries) * CaveHash(Seed, Id, K, 1, 42u) : 320.f * CaveHash(Seed, Id, K, 1, 42u);
        const FVector2D P = Near + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
        const FWorldSample S = Gen.Sample(P);
        if (S.IsWater() || S.WaterDistance < (bDemo ? 15.f : 25.f) || S.Height < 4.f)
        {
            continue;
        }
        bool bPad = false;
        for (const FSettlementPad& Pad : Gen.GetPads())
        {
            bPad |= FVector2D::Distance(P, Pad.Center) < Pad.Radius + 60.0;
        }
        if (bPad)
        {
            continue;
        }
        const float HX = Gen.Height(P + FVector2D(4, 0)) - Gen.Height(P - FVector2D(4, 0));
        const float HY = Gen.Height(P + FVector2D(0, 4)) - Gen.Height(P - FVector2D(0, 4));
        const FVector2D Grad(HX / 8.0, HY / 8.0);
        const float Slope = static_cast<float>(Grad.Size());
        const float Score = -FMath::Abs(Slope - .35f) - (bDemo ? R / 4000.f : 0.f);
        if (Slope > (bDemo ? .06f : .1f) && Slope < 1.2f && Score > BestScore)
        {
            BestScore = Score;
            Best = P;
            BestUp = Grad.GetSafeNormal();
        }
    }
    if (BestScore < -1e8f)
    {
        return; // no suitable hillside here
    }
    FCaveSystem Sys;
    Sys.Id = CaveSystems.Num();
    Sys.bDemo = bDemo;
    Sys.Kind = Kind;
    Sys.Rock = Gen.Sample(Best).Rock;
    Sys.Inward = BestUp;
    const float Ground = Gen.Height(Best);
    Sys.Entrance = FVector(Best.X, Best.Y, Ground);
    const FVector In(BestUp.X, BestUp.Y, 0.0);
    // Entrance tunnel: from just outside the slope, down into the hill.
    const FVector Mouth = Sys.Entrance - In * 5.0 + FVector(0, 0, 1.5);
    const float Deep = Kind == ECaveKind::Giant ? 14.f : 9.f;
    const FVector Inside = Sys.Entrance + In * 17.0 - FVector(0, 0, Deep);
    Sys.Tunnels.Add({ Mouth, Inside, 3.4f, 2.9f });
    auto Chamber = [&Sys](const FVector& C, const FVector& R)
    {
        FCaveChamber Ch;
        Ch.Center = C;
        Ch.Radii = R;
        Ch.FloorZ = static_cast<float>(C.Z - R.Z * .8);
        Sys.Chambers.Add(Ch);
    };
    const FVector2D Side = CaveRotate(BestUp, CaveHash(Seed, Id, 7, 0, 43u) < .5f ? 65.f : -65.f);
    switch (Kind)
    {
    case ECaveKind::Dark:
    {
        // A short passage, then a deep shaft to a lightless chamber and a side gallery.
        const FVector Top = Inside + In * 6.0;
        const FVector Bottom = Top - FVector(0, 0, 28.0);
        Sys.Tunnels.Add({ Inside, Top, 2.9f, 3.2f });
        Sys.Tunnels.Add({ Top, Bottom, 3.2f, 3.6f });
        Chamber(Bottom + In * 6.0 - FVector(0, 0, 2.0), FVector(9, 8, 6));
        const FVector Gallery = Sys.Chambers[0].Center + FVector(Side.X, Side.Y, 0) * 20.0 - FVector(0, 0, 3.0);
        Sys.Tunnels.Add({ Sys.Chambers[0].Center, Gallery, 2.6f, 2.2f });
        Chamber(Gallery, FVector(6, 5, 4));
        break;
    }
    case ECaveKind::Giant:
    {
        const FVector C = Inside + In * 22.0 - FVector(0, 0, 6.0);
        Sys.Tunnels.Add({ Inside, C, 2.9f, 6.f });
        Chamber(C, FVector(24, 20, 13));
        for (int32 K = 0; K < 3; ++K)
        {
            const float A = 2.f * PI * (K + CaveHash(Seed, Id, K, 8, 44u) * .5f) / 3.f;
            const FVector P = C + FVector(FMath::Cos(A) * 11.0, FMath::Sin(A) * 9.0, 0.0);
            Sys.Columns.Add({ P - FVector(0, 0, 14.0), P + FVector(0, 0, 14.0), 1.6f + 1.2f * CaveHash(Seed, Id, K, 9, 45u) });
        }
        const FVector Lake = C + FVector(Side.X, Side.Y, 0) * 30.0 - FVector(0, 0, 6.0);
        Sys.Tunnels.Add({ C, Lake, 4.f, 3.f });
        Chamber(Lake, FVector(12, 10, 6));
        Sys.PoolLevel = Sys.Chambers.Last().FloorZ + 1.2f;
        break;
    }
    default:
    {
        // Natural and enchanted: a first chamber, a winding gallery and a lower second chamber.
        const bool bEnchanted = Kind == ECaveKind::Enchanted;
        const FVector C1 = Inside + In * 10.0 - FVector(0, 0, 2.5);
        Sys.Tunnels.Add({ Inside, C1, 2.9f, 4.f });
        Chamber(C1, bEnchanted ? FVector(13, 11, 7) : FVector(10, 9, 5.5));
        const FVector Bend = C1 + FVector(Side.X, Side.Y, 0) * 14.0 - FVector(0, 0, 2.0);
        const FVector C2 = Bend + FVector(BestUp.X, BestUp.Y, 0) * 12.0 - FVector(0, 0, 3.0);
        Sys.Tunnels.Add({ C1, Bend, 2.6f, 2.4f });
        Sys.Tunnels.Add({ Bend, C2, 2.4f, 3.f });
        Chamber(C2, bEnchanted ? FVector(10, 9, 6) : FVector(7, 6, 4.5));
        if (bEnchanted)
        {
            Sys.PoolLevel = Sys.Chambers.Last().FloorZ + .9f;
        }
        break;
    }
    }
    // Bounds of everything carved, with the walls.
    for (const FCaveTunnel& T : Sys.Tunnels)
    {
        Sys.Bounds += FBox(T.A - FVector(T.RadiusA + 3.f), T.A + FVector(T.RadiusA + 3.f));
        Sys.Bounds += FBox(T.B - FVector(T.RadiusB + 3.f), T.B + FVector(T.RadiusB + 3.f));
    }
    for (const FCaveChamber& C : Sys.Chambers)
    {
        Sys.Bounds += FBox(C.Center - C.Radii - FVector(3.f), C.Center + C.Radii + FVector(3.f));
    }
    CaveSystems.Add(Sys);
    ActivateBox(Sys.Bounds);
}

FIntVector FAethraCaveGen::BlockOf(const FVector& P)
{
    return FIntVector(FMath::FloorToInt(P.X / BlockM), FMath::FloorToInt(P.Y / BlockM), FMath::FloorToInt(P.Z / BlockM));
}

void FAethraCaveGen::ActivateBox(const FBox& Box)
{
    // Columns of blocks over the box, from below it to above the highest ground over its footprint
    // (the volume draws the ground surface there instead of the height field).
    const FIntVector Lo = BlockOf(Box.Min), Hi = BlockOf(Box.Max);
    for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
    {
        for (int32 X = Lo.X; X <= Hi.X; ++X)
        {
            float Top = -1e9f, Bottom = 1e9f;
            for (int32 K = 0; K <= 4; ++K)
            {
                for (int32 L = 0; L <= 4; ++L)
                {
                    const float H = Gen.Height(FVector2D((X + K / 4.0) * BlockM, (Y + L / 4.0) * BlockM));
                    Top = FMath::Max(Top, H);
                    Bottom = FMath::Min(Bottom, H);
                }
            }
            const int32 Z0 = FMath::Min(Lo.Z, FMath::FloorToInt((Bottom - 3.0) / BlockM));
            const int32 Z1 = FMath::FloorToInt((Top + 3.0) / BlockM);
            for (int32 Z = Z0; Z <= Z1; ++Z)
            {
                Blocks.Add(FIntVector(X, Y, Z));
            }
            Columns.Add(FIntPoint(X, Y));
        }
    }
}

bool FAethraCaveGen::CoversColumn(const FVector2D& P, double InsetM) const
{
    for (int32 K = 0; K < 5; ++K)
    {
        const FVector2D Q = K == 4 ? P : P + FVector2D((K & 1) ? InsetM : -InsetM, (K & 2) ? InsetM : -InsetM);
        if (!Columns.Contains(FIntPoint(FMath::FloorToInt(Q.X / BlockM), FMath::FloorToInt(Q.Y / BlockM))))
        {
            return false;
        }
    }
    return true;
}

float FAethraCaveGen::Ground(const FVector2D& P) const
{
    // Bilinear over the 1 m lattice of WorldGen heights (cached): the same surface the chunks draw.
    const int32 X = FMath::FloorToInt(P.X), Y = FMath::FloorToInt(P.Y);
    const float FX = static_cast<float>(P.X - X), FY = static_cast<float>(P.Y - Y);
    float H[4];
    {
        FScopeLock Lock(&CacheLock);
        if (GroundCache.Num() > 400000)
        {
            GroundCache.Reset();
        }
        for (int32 K = 0; K < 4; ++K)
        {
            const FIntPoint C(X + (K & 1), Y + (K >> 1));
            if (const float* Found = GroundCache.Find(C))
            {
                H[K] = *Found;
            }
            else
            {
                H[K] = Gen.Height(FVector2D(C.X, C.Y));
                GroundCache.Add(C, H[K]);
            }
        }
    }
    return FMath::Lerp(FMath::Lerp(H[0], H[1], FX), FMath::Lerp(H[2], H[3], FX), FY);
}

float FAethraCaveGen::Carve(const FVector& P) const
{
    float Best = -1e6f;
    for (const FCaveSystem& S : CaveSystems)
    {
        if (!S.Bounds.ExpandBy(1.0).IsInside(P))
        {
            continue;
        }
        for (const FCaveTunnel& T : S.Tunnels)
        {
            float U = 0.f;
            const float D = CaveSegment(P, T.A, T.B, U);
            Best = FMath::Max(Best, FMath::Lerp(T.RadiusA, T.RadiusB, U) - D);
        }
        for (const FCaveChamber& C : S.Chambers)
        {
            const FVector Q = (P - C.Center) / C.Radii;
            Best = FMath::Max(Best, static_cast<float>((1.0 - Q.Size()) * C.Radii.GetMin()));
        }
    }
    if (Best > -4.f)
    {
        // Rough walls: two octaves of noise (metres), so the rock is never smooth.
        Best += (CaveNoise(Seed, P / 2.6) - .5f) * 1.5f + (CaveNoise(Seed, P / 1.1 + 31.7) - .5f) * .5f;
    }
    return Best;
}

float FAethraCaveGen::Density(const FVector& P) const
{
    float D = Ground(FVector2D(P.X, P.Y)) - static_cast<float>(P.Z);
    const float C = Carve(P);
    D = FMath::Min(D, -C);
    for (const FCaveSystem& S : CaveSystems)
    {
        if (!S.Bounds.IsInside(P))
        {
            continue;
        }
        for (const FCaveColumn& Col : S.Columns)
        {
            float U = 0.f;
            D = FMath::Max(D, Col.Radius * (1.f + .25f * FMath::Sin(static_cast<float>(P.Z) * .7f)) - CaveSegment(P, Col.A, Col.B, U));
        }
    }
    for (const FCaveEdit& E : CaveEdits)
    {
        const float Dist = static_cast<float>(FVector::Dist(P, E.Center));
        D = E.bFill ? FMath::Max(D, E.Radius - Dist) : FMath::Min(D, Dist - E.Radius);
    }
    return D;
}

void FAethraCaveGen::AddEdit(const FCaveEdit& Edit, TArray<FIntVector>& OutBlocks)
{
    CaveEdits.Add(Edit);
    const FBox Box(Edit.Center - FVector(Edit.Radius + 1.5f), Edit.Center + FVector(Edit.Radius + 1.5f));
    const int32 Before = Blocks.Num();
    TSet<FIntVector> Old = Blocks;
    ActivateBox(Box);
    const FIntVector Lo = BlockOf(Box.Min), Hi = BlockOf(Box.Max);
    TSet<FIntVector> Out;
    for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
        for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
            for (int32 X = Lo.X; X <= Hi.X; ++X)
                Out.Add(FIntVector(X, Y, Z));
    if (Blocks.Num() != Before)
    {
        for (const FIntVector& B : Blocks)
        {
            if (!Old.Contains(B))
            {
                Out.Add(B);
            }
        }
    }
    OutBlocks = Out.Array();
}

bool FAethraCaveGen::SaveEdits(const FString& Path) const
{
    TArray<uint8> Bytes;
    FMemoryWriter Ar(Bytes);
    uint32 M = Magic;
    int32 V = Version, Count = CaveEdits.Num();
    Ar << M << V << Count;
    for (FCaveEdit E : CaveEdits)
    {
        uint8 Fill = E.bFill;
        Ar << E.Center << E.Radius << Fill;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const FString Temp = Path + TEXT(".tmp");
    return FFileHelper::SaveArrayToFile(Bytes, *Temp) && IFileManager::Get().Move(*Path, *Temp, true);
}

bool FAethraCaveGen::LoadEdits(const FString& Path)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path))
    {
        return false;
    }
    FMemoryReader Ar(Bytes);
    uint32 M = 0;
    int32 V = 0, Count = 0;
    Ar << M << V << Count;
    if (M != Magic || V < 1 || V > Version || Count < 0 || Count > 1 << 20)
    {
        return false;
    }
    TArray<FCaveEdit> Loaded;
    for (int32 I = 0; I < Count; ++I)
    {
        FCaveEdit E;
        uint8 Fill = 0;
        Ar << E.Center << E.Radius << Fill;
        E.bFill = Fill != 0;
        Loaded.Add(E);
    }
    if (Ar.IsError() || !Ar.AtEnd())
    {
        return false;
    }
    TArray<FIntVector> Ignored;
    for (const FCaveEdit& E : Loaded)
    {
        AddEdit(E, Ignored);
    }
    return true;
}

void FAethraCaveGen::MeshBlock(const FIntVector& Block, FCaveMesh& Out) const
{
    // Surface Nets on a 1 m lattice: one vertex per cell where the density changes sign (the mean of the
    // edge crossings), one quad per sign-changing edge; flat-shaded triangles. Samples -1..17 per axis
    // so the block owns edges whose lower end is in 0..15 and every quad's four cells exist.
    constexpr int32 S = BlockCells + 3; // samples per axis
    const FVector Origin = FVector(Block) * BlockM;
    TArray<float> D;
    D.SetNumUninitialized(S * S * S);
    TArray<float> G;
    G.SetNumUninitialized(S * S);
    for (int32 J = 0; J < S; ++J)
    {
        for (int32 I = 0; I < S; ++I)
        {
            G[J * S + I] = Ground(FVector2D(Origin.X + (I - 1) * CellM, Origin.Y + (J - 1) * CellM));
        }
    }
    bool bAny = false, bPositive = false, bNegative = false;
    for (int32 K = 0; K < S; ++K)
    {
        for (int32 J = 0; J < S; ++J)
        {
            for (int32 I = 0; I < S; ++I)
            {
                const FVector P = Origin + FVector(I - 1, J - 1, K - 1) * CellM;
                float V = G[J * S + I] - static_cast<float>(P.Z);
                // Same as Density() but with the cached ground of this block.
                if (V > -6.f)
                {
                    V = FMath::Min(V, -Carve(P));
                    for (const FCaveSystem& Sys : CaveSystems)
                    {
                        if (Sys.Bounds.IsInside(P))
                        {
                            for (const FCaveColumn& Col : Sys.Columns)
                            {
                                float U = 0.f;
                                V = FMath::Max(V, Col.Radius * (1.f + .25f * FMath::Sin(static_cast<float>(P.Z) * .7f)) - CaveSegment(P, Col.A, Col.B, U));
                            }
                        }
                    }
                    for (const FCaveEdit& E : CaveEdits)
                    {
                        const float Dist = static_cast<float>(FVector::Dist(P, E.Center));
                        V = E.bFill ? FMath::Max(V, E.Radius - Dist) : FMath::Min(V, Dist - E.Radius);
                    }
                }
                if (FMath::Abs(V) < 1e-4f)
                {
                    V = 1e-4f; // never exactly zero: sign tests stay consistent
                }
                D[(K * S + J) * S + I] = V;
                bPositive |= V > 0.f;
                bNegative |= V < 0.f;
            }
        }
    }
    bAny = bPositive && bNegative;
    if (!bAny)
    {
        return;
    }
    auto Idx = [S](int32 I, int32 J, int32 K) { return (K * S + J) * S + I; };
    // Cell vertices (cells -1..16 in sample space 0..S-2).
    constexpr int32 C = S - 1;
    TArray<int32> CellVertex;
    CellVertex.Init(-1, C * C * C);
    TArray<FVector> Verts;
    static const int32 Edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    for (int32 K = 0; K < C; ++K)
    {
        for (int32 J = 0; J < C; ++J)
        {
            for (int32 I = 0; I < C; ++I)
            {
                float V[8];
                for (int32 N = 0; N < 8; ++N)
                {
                    V[N] = D[Idx(I + (N & 1), J + ((N >> 1) & 1), K + (N >> 2))];
                }
                FVector Sum = FVector::ZeroVector;
                int32 Count = 0;
                for (const auto& E : Edges)
                {
                    if ((V[E[0]] > 0.f) != (V[E[1]] > 0.f))
                    {
                        const float T = V[E[0]] / (V[E[0]] - V[E[1]]);
                        const FVector A(E[0] & 1, (E[0] >> 1) & 1, E[0] >> 2), B(E[1] & 1, (E[1] >> 1) & 1, E[1] >> 2);
                        Sum += FMath::Lerp(A, B, T);
                        ++Count;
                    }
                }
                if (Count)
                {
                    CellVertex[(K * C + J) * C + I] = Verts.Num();
                    // Snap a little toward the cell grid: crisper, more geometric facets.
                    const FVector Local = Sum / Count;
                    const FVector Snapped = FMath::Lerp(Local, FVector(FMath::RoundToDouble(Local.X * 2.0) * .5, FMath::RoundToDouble(Local.Y * 2.0) * .5, FMath::RoundToDouble(Local.Z * 2.0) * .5), .35);
                    Verts.Add(Origin + (FVector(I - 1, J - 1, K - 1) + Snapped) * CellM);
                }
            }
        }
    }
    // Which system a point belongs to (for its rock and kind).
    auto SystemAt = [this](const FVector& P) -> const FCaveSystem*
    {
        for (const FCaveSystem& Sys : CaveSystems)
        {
            if (Sys.Bounds.IsInside(P))
            {
                return &Sys;
            }
        }
        return nullptr;
    };
    const FWorldSample BlockSample = Gen.Sample(FVector2D(Origin.X + BlockM * .5, Origin.Y + BlockM * .5));
    const EAmeRock DefaultRock = BlockSample.Rock;
    // The soil the height-field terrain shows here, so the volume and the terrain match at the seam.
    FAmeEnvironment SoilEnv;
    SoilEnv.Biome = BlockSample.Biome;
    SoilEnv.Rock = BlockSample.Rock;
    SoilEnv.Moisture = BlockSample.Moisture;
    SoilEnv.Temperature = BlockSample.Temperature;
    SoilEnv.WaterDistance = BlockSample.WaterDistance;
    const EAmeSurface SurfaceSoil = FAmeSelector::Terrain(SoilEnv, Seed, FIntPoint(Block.X, Block.Y)).Primary;
    auto Emit = [&](const FVector& A, const FVector& B, const FVector& Cc, const FVector& AirSide)
    {
        FVector N = FVector::CrossProduct(B - A, Cc - A).GetSafeNormal();
        if (N.IsNearlyZero())
        {
            return;
        }
        FVector P0 = A, P1 = B, P2 = Cc;
        if (FVector::DotProduct(N, AirSide) < 0.0)
        {
            N = -N;
            Swap(P1, P2);
        }
        const FVector Center = (A + B + Cc) / 3.0;
        const float GroundZ = Ground(FVector2D(Center.X, Center.Y));
        const FCaveSystem* Sys = SystemAt(Center);
        const EAmeRock Rock = Sys ? Sys->Rock : DefaultRock;
        EAmeSurface Surface = FAmeSelector::RockSurface(Rock);
        if (Center.Z > GroundZ - 1.2f && N.Z > .3)
        {
            Surface = SurfaceSoil; // the surface itself (around an entrance or a dig), as the terrain chose it
        }
        else if (Center.Z > GroundZ - 2.2f)
        {
            Surface = EAmeSurface::Dirt; // the soil layer under it
        }
        else if (N.Z > .6)
        {
            const float H = CaveHash(Seed, FMath::FloorToInt(Center.X / 2.0), FMath::FloorToInt(Center.Y / 2.0), FMath::FloorToInt(Center.Z), 51u);
            Surface = Sys && Sys->Kind == ECaveKind::Enchanted && H < .3f ? EAmeSurface::GlowMoss : (H < .55f ? EAmeSurface::Gravel : Surface);
        }
        const FAmeMaterialDef& Def = FAmeRegistry::Get(Surface);
        // Depth darkens the albedo a little (less bounce light), ceilings more than floors.
        const float Shade = FMath::Clamp(1.f - FMath::Max(GroundZ - static_cast<float>(Center.Z), 0.f) * .01f, .7f, 1.f) * (N.Z < -.4 ? .85f : 1.f);
        FLinearColor Albedo = Def.Base * Shade;
        Albedo.A = Def.Roughness;
        for (const FVector& P : { P0, P1, P2 })
        {
            Out.Triangles.Add(Out.Positions.Num());
            Out.Positions.Add(P * 100.0);
            Out.Normals.Add(N);
            Out.UV0.Add(FVector2D(P.X, P.Y) / 4.0);
            Out.UV1.Add(FVector2D(static_cast<double>(Surface), static_cast<double>(Surface)));
            Out.UV2.Add(FVector2D(1.0, 0.0));
            Out.Colors.Add(Albedo);
        }
    };
    auto CellV = [&](int32 I, int32 J, int32 K) -> int32
    {
        if (I < 0 || J < 0 || K < 0 || I >= C || J >= C || K >= C)
        {
            return -1;
        }
        return CellVertex[(K * C + J) * C + I];
    };
    // Edges owned by this block: lower sample in 1..16 (block-local 0..15) on all three axes.
    for (int32 K = 1; K <= BlockCells; ++K)
    {
        for (int32 J = 1; J <= BlockCells; ++J)
        {
            for (int32 I = 1; I <= BlockCells; ++I)
            {
                const float V0 = D[Idx(I, J, K)];
                for (int32 Axis = 0; Axis < 3; ++Axis)
                {
                    const int32 I1 = I + (Axis == 0), J1 = J + (Axis == 1), K1 = K + (Axis == 2);
                    const float V1 = D[Idx(I1, J1, K1)];
                    if ((V0 > 0.f) == (V1 > 0.f))
                    {
                        continue;
                    }
                    // The four cells around the edge (cell index = sample index of its lower corner).
                    int32 Q[4];
                    if (Axis == 0)
                    {
                        Q[0] = CellV(I, J - 1, K - 1); Q[1] = CellV(I, J, K - 1); Q[2] = CellV(I, J, K); Q[3] = CellV(I, J - 1, K);
                    }
                    else if (Axis == 1)
                    {
                        Q[0] = CellV(I - 1, J, K - 1); Q[1] = CellV(I - 1, J, K); Q[2] = CellV(I, J, K); Q[3] = CellV(I, J, K - 1);
                    }
                    else
                    {
                        Q[0] = CellV(I - 1, J - 1, K); Q[1] = CellV(I, J - 1, K); Q[2] = CellV(I, J, K); Q[3] = CellV(I - 1, J, K);
                    }
                    if (Q[0] < 0 || Q[1] < 0 || Q[2] < 0 || Q[3] < 0)
                    {
                        continue;
                    }
                    const FVector AxisDir(Axis == 0, Axis == 1, Axis == 2);
                    const FVector Air = V0 > 0.f ? AxisDir : -AxisDir; // rock at the lower end: air is ahead
                    Emit(Verts[Q[0]], Verts[Q[1]], Verts[Q[2]], Air);
                    Emit(Verts[Q[0]], Verts[Q[2]], Verts[Q[3]], Air);
                }
            }
        }
    }
}

void FAethraCaveGen::Formations(const FCaveSystem& Sys, TArray<FFormation>& Out) const
{
    for (int32 Ci = 0; Ci < Sys.Chambers.Num(); ++Ci)
    {
        const FCaveChamber& Ch = Sys.Chambers[Ci];
        const int32 Count = Sys.Kind == ECaveKind::Giant ? 34 : 16;
        for (int32 K = 0; K < Count; ++K)
        {
            const float A = 2.f * PI * CaveHash(Seed, Sys.Id, Ci, K, 61u);
            const float R = .8f * FMath::Sqrt(CaveHash(Seed, Sys.Id, Ci, K, 62u));
            const FVector Start = Ch.Center + FVector(FMath::Cos(A) * Ch.Radii.X * R, FMath::Sin(A) * Ch.Radii.Y * R, 0.0);
            if (!IsAir(Start))
            {
                continue;
            }
            // Ceiling: march up to the rock.
            for (float Z = 0.f; Z < Ch.Radii.Z * 2.f; Z += .25f)
            {
                if (!IsAir(Start + FVector(0, 0, Z)))
                {
                    Out.Add({ Start + FVector(0, 0, Z - .1f), FVector(0, 0, -1), .7f + 1.6f * CaveHash(Seed, Sys.Id, Ci, K, 63u), 0 });
                    break;
                }
            }
            // Floor: some stalagmites under them.
            if (CaveHash(Seed, Sys.Id, Ci, K, 64u) < .45f)
            {
                for (float Z = 0.f; Z < Ch.Radii.Z * 2.f; Z += .25f)
                {
                    const FVector P = Start - FVector(0, 0, Z);
                    if (!IsAir(P))
                    {
                        if (P.Z > Sys.PoolLevel)
                        {
                            Out.Add({ P + FVector(0, 0, .1f), FVector(0, 0, 1), .5f + 1.1f * CaveHash(Seed, Sys.Id, Ci, K, 65u), 1 });
                        }
                        break;
                    }
                }
            }
        }
        if (Sys.Kind == ECaveKind::Enchanted || (Sys.Kind == ECaveKind::Giant && Ci == 0))
        {
            // Aetherite crystals grow from the walls and the floor toward the middle of the chamber.
            for (int32 K = 0; K < 14; ++K)
            {
                const float A = 2.f * PI * CaveHash(Seed, Sys.Id, Ci, K, 66u);
                const float E = -.6f + .9f * CaveHash(Seed, Sys.Id, Ci, K, 67u);
                const FVector Dir = FVector(FMath::Cos(A) * FMath::Cos(E), FMath::Sin(A) * FMath::Cos(E), FMath::Sin(E));
                for (float T = 0.f; T < Ch.Radii.GetMax() * 1.5f; T += .25f)
                {
                    const FVector P = Ch.Center + Dir * T;
                    if (!IsAir(P))
                    {
                        Out.Add({ P - Dir * .1f, (-Dir + FVector(0, 0, .6)).GetSafeNormal(), .8f + 1.4f * CaveHash(Seed, Sys.Id, Ci, K, 68u), 2 });
                        break;
                    }
                }
            }
        }
    }
}
