// Voxel navigation for LIS NPCs: a walkability cache over terrain columns plus A*.
// Columns are computed lazily and invalidated one by one when a block changes, so a player
// edit never rebuilds the whole map. Since LIS 03 a column keeps up to MaxLevels standing levels
// (ground under a roof, the roof, a bridge...) and a bit mask of the cells a body can occupy, so
// NPCs walk through doorways and under roofs instead of over them.
#pragma once

#include "AethraVoxelTypes.h"

class AAethraVoxelWorld;

struct FAethraNavColumn
{
    static constexpr int32 MaxLevels = 4;
    int16 Levels[MaxLevels] = { -1, -1, -1, -1 }; // feet cells, bottom to top (the highest kept)
    uint8 NumLevels = 0;
    int16 StandZ = -1;      // highest level (the old heightfield view); -1 = not walkable
    int16 TrunkBaseZ = -1;  // lowest wood cell of a tree trunk in this column, -1 if none
    bool bWater = false;    // top non-air block is water
    bool bKnown = false;
    uint64 Open[2] = { 0, 0 };   // bit Z set = a body can occupy cell Z (air or a cross plant)
    uint64 Leaves[2] = { 0, 0 }; // bit Z set = foliage: no room for a body, but a head brushes through it

    bool IsOpen(int32 Z) const { return Z >= 0 && Z < 128 && (Open[Z >> 6] >> (Z & 63)) & 1; }
    bool IsClear(int32 Z) const { return IsOpen(Z) || (Z >= 0 && Z < 128 && (Leaves[Z >> 6] >> (Z & 63)) & 1); }
    bool HasLevel(int32 Z) const;
};

class FAethraLisNav
{
public:
    void Initialize(const AAethraVoxelWorld* InWorld, FIntPoint InCenter, int32 InHalfSize);

    bool InBounds(FIntPoint P) const;
    const FAethraNavColumn& Column(FIntPoint P);
    bool Walkable(FIntPoint P) { return InBounds(P) && Column(P).NumLevels > 0; }
    bool IsStand(const FIntVector& Cell) { return InBounds(FIntPoint(Cell.X, Cell.Y)) && Column(FIntPoint(Cell.X, Cell.Y)).HasLevel(Cell.Z); }
    // Highest standing cell (feet) of a walkable column.
    FIntVector Stand(FIntPoint P) { return FIntVector(P.X, P.Y, Column(P).StandZ); }
    // The level of column P closest to Z (inside a house: the floor, not the roof).
    FIntVector StandNear(FIntPoint P, int32 Z);

    // A block changed: forget that column only. Returns the new revision.
    int64 Invalidate(FIntPoint P);
    int64 Revision() const { return NavRevision; }

    // A* between standing cells: steps up 1 block, down up to 2, needs head room for the body
    // (2 cells, 3 when stepping up) and never cuts a corner diagonally. Empty = unreachable.
    bool FindPath(const FIntVector& From, const FIntVector& To, TArray<FIntVector>& OutPath, int32 MaxExpansions = 6000);
    // Heightfield form kept for callers that only know columns: top level to top level.
    bool FindPath(FIntPoint From, FIntPoint To, TArray<FIntVector>& OutPath, int32 MaxExpansions = 6000);
    bool CanStep(const FIntVector& From, const FIntVector& To);
    bool CanStep(FIntPoint From, FIntPoint To) { return Walkable(From) && Walkable(To) && CanStep(Stand(From), Stand(To)); }

    // Nearest walkable column to P within Radius (ring search), or false.
    bool NearestWalkable(FIntPoint P, int32 Radius, FIntPoint& Out);
    // Nearest standing cell to Cell (ring search over columns, closest level in height).
    bool NearestStand(const FIntVector& Cell, int32 Radius, FIntVector& Out);

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
    bool OpenAt(FIntPoint P, int32 Z) { return InBounds(P) && Column(P).IsOpen(Z); }
    bool ClearAt(FIntPoint P, int32 Z) { return InBounds(P) && Column(P).IsClear(Z); }
};
