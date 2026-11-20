// Voxel navigation for LIS NPCs: a walkability cache over terrain columns plus A*.
// Columns are computed lazily and invalidated one by one when a block changes, so a player
// edit never rebuilds the whole map. The grid is a heightfield: one standing level per column.
#pragma once

#include "AethraVoxelTypes.h"

class AAethraVoxelWorld;

struct FAethraNavColumn
{
    int16 StandZ = -1;      // cell the feet occupy; -1 = not walkable
    int16 TrunkBaseZ = -1;  // lowest wood cell of a tree trunk in this column, -1 if none
    bool bWater = false;    // top non-air block is water
    bool bKnown = false;
};

class FAethraLisNav
{
public:
    void Initialize(const AAethraVoxelWorld* InWorld, FIntPoint InCenter, int32 InHalfSize);

    bool InBounds(FIntPoint P) const;
    const FAethraNavColumn& Column(FIntPoint P);
    bool Walkable(FIntPoint P) { return InBounds(P) && Column(P).StandZ >= 0; }
    // Standing cell (feet) of a walkable column, for movement targets.
    FIntVector Stand(FIntPoint P) { return FIntVector(P.X, P.Y, Column(P).StandZ); }

    // A block changed: forget that column only. Returns the new revision.
    int64 Invalidate(FIntPoint P);
    int64 Revision() const { return NavRevision; }

    // A* over walkable columns: steps up 1 block, down up to 2, diagonals only around clear corners.
    // Empty result = unreachable (or the search budget ran out).
    bool FindPath(FIntPoint From, FIntPoint To, TArray<FIntVector>& OutPath, int32 MaxExpansions = 6000);
    bool CanStep(FIntPoint From, FIntPoint To);

    // Nearest walkable column to P within Radius (spiral search), or false.
    bool NearestWalkable(FIntPoint P, int32 Radius, FIntPoint& Out);

    int32 ColumnsComputed = 0;
    int32 PathsSearched = 0;
    int32 PathsFailed = 0;

private:
    const AAethraVoxelWorld* World = nullptr;
    FIntPoint Center = FIntPoint::ZeroValue;
    int32 HalfSize = 0;
    int32 Side = 0;
    int64 NavRevision = 0;
    TArray<FAethraNavColumn> Cache;
    TArray<EAethraBlock> ColumnScratch;

    int32 Slot(FIntPoint P) const { return (P.Y - Center.Y + HalfSize) * Side + (P.X - Center.X + HalfSize); }
    void Compute(FIntPoint P, FAethraNavColumn& Out);
};
