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

// Feet and head cells: air and cross-shaped plants do not block a walker.
bool IsPassable(EAethraBlock Block)
{
    return Block == EAethraBlock::Air || AethraTerrain::IsPlant(Block);
}
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

    // From the sky down: leaves and plants are seen through; the first water or opaque block decides.
    for (int32 Z = Height - 1; Z > 0; --Z)
    {
        const EAethraBlock Block = ColumnScratch[Z];
        if (Block == EAethraBlock::Water)
        {
            Out.bWater = true;
            return;
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
            return; // a tree trunk stands here
        }
        // Solid ground: walkable if feet and head are clear.
        if (Z + 2 < Height && IsPassable(ColumnScratch[Z + 1]) && IsPassable(ColumnScratch[Z + 2]))
        {
            Out.StandZ = static_cast<int16>(Z + 1);
        }
        return;
    }
}

int64 FAethraLisNav::Invalidate(FIntPoint P)
{
    if (InBounds(P))
    {
        Cache[Slot(P)].bKnown = false;
    }
    return ++NavRevision;
}

bool FAethraLisNav::CanStep(FIntPoint From, FIntPoint To)
{
    if (!Walkable(From) || !Walkable(To))
    {
        return false;
    }
    const int32 Rise = Column(To).StandZ - Column(From).StandZ;
    if (Rise > 1 || Rise < -2)
    {
        return false;
    }
    const FIntPoint D = To - From;
    if (D.X != 0 && D.Y != 0)
    {
        // Diagonal: both side columns must be steppable too, so the body does not clip a corner.
        const FIntPoint SideA(From.X + D.X, From.Y), SideB(From.X, From.Y + D.Y);
        for (const FIntPoint SidePoint : { SideA, SideB })
        {
            if (!Walkable(SidePoint) || FMath::Abs(Column(SidePoint).StandZ - Column(From).StandZ) > 1)
            {
                return false;
            }
        }
    }
    return true;
}

bool FAethraLisNav::FindPath(FIntPoint From, FIntPoint To, TArray<FIntVector>& OutPath, int32 MaxExpansions)
{
    ++PathsSearched;
    OutPath.Reset();
    if (!Walkable(From) || !Walkable(To))
    {
        ++PathsFailed;
        return false;
    }
    if (From == To)
    {
        OutPath.Add(Stand(To));
        return true;
    }

    struct FNode
    {
        FIntPoint P;
        float G;
        float F;
    };
    auto Heuristic = [To](FIntPoint P)
    {
        const int32 DX = FMath::Abs(P.X - To.X), DY = FMath::Abs(P.Y - To.Y);
        return static_cast<float>(FMath::Max(DX, DY)) + .414f * FMath::Min(DX, DY);
    };
    TArray<FNode> Open;
    TMap<FIntPoint, float> Best;
    TMap<FIntPoint, FIntPoint> Parent;
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
            for (FIntPoint P = To; P != From; P = Parent[P])
            {
                OutPath.Add(Stand(P));
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
            const FIntPoint Next = Node.P + Step;
            if (!CanStep(Node.P, Next))
            {
                continue;
            }
            // Climbing costs a little more, so flat detours win when they are short.
            const float Climb = Column(Next).StandZ > Column(Node.P).StandZ ? .5f : 0.f;
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
