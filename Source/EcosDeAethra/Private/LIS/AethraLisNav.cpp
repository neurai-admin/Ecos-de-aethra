#include "AethraLisNav.h"

#include "Algo/Reverse.h"
#include "AethraVoxelWorld.h"
#include "Voxel/AethraTerrain.h"

namespace
{
bool IsWood(EAethraBlock Block)
{
    return Block == EAethraBlock::Wood || Block == EAethraBlock::BirchWood;
}

// A body can occupy air and cross-shaped plants.
bool IsOpenBlock(EAethraBlock Block)
{
    return Block == EAethraBlock::Air || AethraTerrain::IsPlant(Block);
}

// Feet rest on solid ground. Not on water (no swimming yet), not on leaves and not on wood: tree
// tops inside a crown, roofs and workbenches are never places to stand (village floors are stone or soil).
bool CanStandOn(EAethraBlock Block)
{
    return !IsOpenBlock(Block) && Block != EAethraBlock::Water && !AethraTerrain::IsLeaves(Block) && !IsWood(Block);
}
}

bool FAethraNavColumn::HasLevel(int32 Z) const
{
    for (int32 I = 0; I < NumLevels; ++I)
    {
        if (Levels[I] == Z)
        {
            return true;
        }
    }
    return false;
}

void FAethraLisNav::Initialize(const AAethraVoxelWorld* InWorld, FIntPoint InCenter, int32 InHalfSize)
{
    World = InWorld;
    Center = InCenter;
    HalfSize = InHalfSize;
    Side = 2 * HalfSize + 1;
    Cache.Reset();
    Cache.SetNum(Side * Side);
    ++NavRevision;
}

bool FAethraLisNav::InBounds(FIntPoint P) const
{
    return FMath::Abs(P.X - Center.X) <= HalfSize && FMath::Abs(P.Y - Center.Y) <= HalfSize;
}

const FAethraNavColumn& FAethraLisNav::Column(FIntPoint P)
{
    static const FAethraNavColumn Blocked;
    if (!InBounds(P) || !World)
    {
        return Blocked;
    }
    FAethraNavColumn& Entry = Cache[Slot(P)];
    if (!Entry.bKnown)
    {
        Compute(P, Entry);
    }
    return Entry;
}

void FAethraLisNav::Compute(FIntPoint P, FAethraNavColumn& Out)
{
    using namespace AethraTerrain;
    ++ColumnsComputed;
    Out = FAethraNavColumn();
    Out.bKnown = true;
    World->GetColumn(P, ColumnScratch);

    for (int32 Z = 0; Z < Height; ++Z)
    {
        if (IsOpenBlock(ColumnScratch[Z]))
        {
            Out.Open[Z >> 6] |= uint64(1) << (Z & 63);
        }
        else if (IsLeaves(ColumnScratch[Z]))
        {
            Out.Leaves[Z >> 6] |= uint64(1) << (Z & 63);
        }
    }
    // Every cell with solid footing below and room for a 2-block body is a level; keep the highest few.
    TArray<int16, TInlineAllocator<8>> Found;
    for (int32 Z = 1; Z + 1 < Height; ++Z)
    {
        if (CanStandOn(ColumnScratch[Z - 1]) && Out.IsOpen(Z) && Out.IsOpen(Z + 1))
        {
            Found.Add(static_cast<int16>(Z));
        }
    }
    const int32 First = FMath::Max(0, Found.Num() - FAethraNavColumn::MaxLevels);
    for (int32 I = First; I < Found.Num(); ++I)
    {
        Out.Levels[Out.NumLevels++] = Found[I];
    }
    Out.StandZ = Out.NumLevels ? Out.Levels[Out.NumLevels - 1] : -1;

    // Seen from the sky: leaves and plants are looked through; the first water or opaque block tells
    // whether this is water or a tree trunk.
    for (int32 Z = Height - 1; Z > 0; --Z)
    {
        const EAethraBlock Block = ColumnScratch[Z];
        if (Block == EAethraBlock::Water)
        {
            Out.bWater = true;
            break;
        }
        if (!IsOpaque(Block))
        {
            continue;
        }
        if (IsWood(Block))
        {
            int32 Base = Z;
            while (Base > 1 && IsWood(ColumnScratch[Base - 1]))
            {
                --Base;
            }
            Out.TrunkBaseZ = static_cast<int16>(Base);
        }
        break;
    }
}

FIntVector FAethraLisNav::StandNear(FIntPoint P, int32 Z)
{
    const FAethraNavColumn& C = Column(P);
    int32 Best = C.StandZ;
    for (int32 I = 0; I < C.NumLevels; ++I)
    {
        if (FMath::Abs(C.Levels[I] - Z) < FMath::Abs(Best - Z))
        {
            Best = C.Levels[I];
        }
    }
    return FIntVector(P.X, P.Y, Best);
}

int64 FAethraLisNav::Invalidate(FIntPoint P)
{
    if (InBounds(P))
    {
        Cache[Slot(P)].bKnown = false;
    }
    return ++NavRevision;
}

bool FAethraLisNav::CanStep(const FIntVector& From, const FIntVector& To)
{
    const FIntPoint A(From.X, From.Y), B(To.X, To.Y);
    const FIntPoint D = B - A;
    if (FMath::Abs(D.X) > 1 || FMath::Abs(D.Y) > 1 || !InBounds(B) || !Column(B).HasLevel(To.Z))
    {
        return false;
    }
    const int32 Rise = To.Z - From.Z;
    if (Rise > 1 || Rise < -2)
    {
        return false;
    }
    if (Rise == 1 && !ClearAt(A, From.Z + 2))
    {
        return false; // no head room to climb out of here (foliage is fine)
    }
    // Stepping down: the body passes through the destination column at its current height.
    for (int32 Z = To.Z + 2; Z <= From.Z + 1; ++Z)
    {
        if (!ClearAt(B, Z))
        {
            return false;
        }
    }
    if (D.X != 0 && D.Y != 0)
    {
        // Diagonal: the body sweeps both side cells, so they must be open at the higher of the two heights.
        const int32 Low = FMath::Max(From.Z, To.Z);
        for (const FIntPoint SidePoint : { FIntPoint(A.X + D.X, A.Y), FIntPoint(A.X, A.Y + D.Y) })
        {
            if (!OpenAt(SidePoint, Low) || !OpenAt(SidePoint, Low + 1))
            {
                return false;
            }
        }
    }
    return true;
}

bool FAethraLisNav::FindPath(FIntPoint From, FIntPoint To, TArray<FIntVector>& OutPath, int32 MaxExpansions)
{
    if (!Walkable(From) || !Walkable(To))
    {
        ++PathsSearched;
        ++PathsFailed;
        OutPath.Reset();
        return false;
    }
    return FindPath(Stand(From), Stand(To), OutPath, MaxExpansions);
}

bool FAethraLisNav::FindPath(const FIntVector& From, const FIntVector& To, TArray<FIntVector>& OutPath, int32 MaxExpansions)
{
    ++PathsSearched;
    OutPath.Reset();
    if (!IsStand(From) || !IsStand(To))
    {
        ++PathsFailed;
        return false;
    }
    if (From == To)
    {
        OutPath.Add(To);
        return true;
    }

    struct FNode
    {
        FIntVector P;
        float G;
        float F;
    };
    auto Heuristic = [&To](const FIntVector& P)
    {
        const int32 DX = FMath::Abs(P.X - To.X), DY = FMath::Abs(P.Y - To.Y);
        return static_cast<float>(FMath::Max(DX, DY)) + .414f * FMath::Min(DX, DY) + .5f * FMath::Abs(P.Z - To.Z);
    };
    TArray<FNode> Open;
    TMap<FIntVector, float> Best;
    TMap<FIntVector, FIntVector> Parent;
    auto Less = [](const FNode& A, const FNode& B) { return A.F < B.F; };
    Open.HeapPush({ From, 0.f, Heuristic(From) }, Less);
    Best.Add(From, 0.f);

    static const FIntPoint Steps[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
    int32 Expansions = 0;
    while (Open.Num() && Expansions++ < MaxExpansions)
    {
        FNode Node;
        Open.HeapPop(Node, Less);
        if (Node.P == To)
        {
            for (FIntVector P = To; P != From; P = Parent[P])
            {
                OutPath.Add(P);
            }
            Algo::Reverse(OutPath);
            return true;
        }
        if (Node.G > Best.FindRef(Node.P))
        {
            continue; // stale heap entry
        }
        for (const FIntPoint& Step : Steps)
        {
            const FIntPoint NextColumn(Node.P.X + Step.X, Node.P.Y + Step.Y);
            if (!InBounds(NextColumn))
            {
                continue;
            }
            const FAethraNavColumn& C = Column(NextColumn);
            for (int32 L = 0; L < C.NumLevels; ++L)
            {
                const FIntVector Next(NextColumn.X, NextColumn.Y, C.Levels[L]);
                if (!CanStep(Node.P, Next))
                {
                    continue;
                }
                // Climbing costs a little more, so flat detours win when they are short.
                const float Climb = Next.Z > Node.P.Z ? .5f : Next.Z < Node.P.Z ? .2f : 0.f;
                const float G = Node.G + (Step.X && Step.Y ? 1.414f : 1.f) + Climb;
                const float* Known = Best.Find(Next);
                if (!Known || G < *Known)
                {
                    Best.Add(Next, G);
                    Parent.Add(Next, Node.P);
                    Open.HeapPush({ Next, G, G + Heuristic(Next) }, Less);
                }
            }
        }
    }
    ++PathsFailed;
    return false;
}

bool FAethraLisNav::NearestWalkable(FIntPoint P, int32 Radius, FIntPoint& Out)
{
    for (int32 R = 0; R <= Radius; ++R)
    {
        for (int32 Y = -R; Y <= R; ++Y)
        {
            for (int32 X = -R; X <= R; ++X)
            {
                if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != R)
                {
                    continue; // ring only
                }
                const FIntPoint Candidate = P + FIntPoint(X, Y);
                if (Walkable(Candidate))
                {
                    Out = Candidate;
                    return true;
                }
            }
        }
    }
    return false;
}

bool FAethraLisNav::NearestStand(const FIntVector& Cell, int32 Radius, FIntVector& Out)
{
    for (int32 R = 0; R <= Radius; ++R)
    {
        bool bFound = false;
        int32 BestScore = MAX_int32;
        for (int32 Y = -R; Y <= R; ++Y)
        {
            for (int32 X = -R; X <= R; ++X)
            {
                if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != R)
                {
                    continue;
                }
                const FIntPoint Candidate(Cell.X + X, Cell.Y + Y);
                if (!Walkable(Candidate))
                {
                    continue;
                }
                const FIntVector Stand = StandNear(Candidate, Cell.Z);
                const int32 Score = FMath::Abs(Stand.Z - Cell.Z) * 4 + X * X + Y * Y;
                if (Score < BestScore)
                {
                    BestScore = Score;
                    Out = Stand;
                    bFound = true;
                }
            }
        }
        if (bFound)
        {
            return true;
        }
    }
    return false;
}
