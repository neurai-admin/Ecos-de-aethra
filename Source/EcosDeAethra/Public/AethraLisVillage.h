// Elden village in the voxel world (LIS 03): a small, controlled layout for the 20 villagers.
//
// Rules: the village is planned only on ground the player never edited, and it is built only into
// air or vegetation the terrain generated there (grass tufts, flowers). Solid blocks, trees, water
// and every player edit stay untouched: a blocked cell is skipped, not cleared.
// Exception, only in the isolated demo world (-AethraElden, bClearTrees): generated trees standing on
// the plots are felled first (the generator covers almost all dry land in forest). Never a player edit.
// All changes go through AAethraVoxelWorld::SetBlockBy with an Environment instigator, so they are
// saved like any edit, and Remove() puts back what the terrain had (trees included).
// The layout (sites) lives in the LIS save; the blocks derive from it, so a restart rebuilds the
// same lookup without reading the blocks back.
#pragma once

#include "AethraVoxelTypes.h"
#include "LisTypes.h"

class AAethraVoxelWorld;
class FAethraLisNav;

enum class EAethraSite : uint8
{
    Square,     // well and meeting point (the LIS Village anchor)
    House,      // four residents each
    Farm,       // fenced plot with crop rows (the LIS Fields anchor)
    Workshop,   // open shed with a workbench (builders)
    Storage,    // closed store with the village wood
    Market,     // stall (the LIS Market anchor)
    RestArea,   // bench and fire pit
    Built = 200 // marker: construction finished; never build again (player changes stay)
};

struct FAethraVillageOp
{
    FIntVector Cell = FIntVector::ZeroValue;
    EAethraBlock Block = EAethraBlock::Air;
    bool bTree = false; // fells a generated tree block (demo village only)
};

class ECOSDEAETHRA_API FAethraLisVillage
{
public:
    static constexpr int32 EnvironmentId = 1; // FAethraInstigator::Environment id of the village builder
    static constexpr int32 Houses = 5;
    static constexpr int32 DemoSeed = 673; // -AethraElden world: gentle slopes near the spawn

    // Chooses the sites around Center on untouched ground. Empty result = no room (the LIS 02 anchors are used).
    static TArray<FLisSite> Plan(const AAethraVoxelWorld& World, FAethraLisNav& Nav, FIntPoint Center, FString& OutReport, bool bClearTrees = false);

    // Prepares the block list and the cell lookup for these sites (call after Plan or after loading).
    void Setup(const AAethraVoxelWorld& World, const TArray<FLisSite>& InSites, FAethraLisNav* Nav = nullptr);
    bool IsBuilt() const;
    void MarkBuilt() { Sites.Add({ static_cast<uint8>(EAethraSite::Built), 0, 0, FIntVector::ZeroValue }); }
    // Places up to Budget pending blocks. Returns how many were placed; false in bDone when work remains.
    int32 Build(AAethraVoxelWorld& World, int32 Budget, bool& bDone);
    // Puts the generated block back wherever the village block is still in place. Returns cells restored.
    int32 Remove(AAethraVoxelWorld& World);

    bool IsVillageCell(const FIntVector& Cell) const { return Expected.Contains(Cell); }
    bool IsCropCell(const FIntVector& Cell) const { return Crops.Contains(Cell); }
    const EAethraBlock* ExpectedAt(const FIntVector& Cell) const { return Expected.Find(Cell); }
    int32 BlockCount() const { return Expected.Num(); }
    const FLisSite* FindSite(EAethraSite Kind, int32 Index = 0) const;
    const TArray<FLisSite>& GetSites() const { return Sites; }
    const TArray<FIntVector>& CropCells() const { return Crops; }

    // Functional spots (feet cells; the director snaps them to the nav grid).
    FIntVector SquareSpot(int32 Slot) const;        // around the well
    FIntVector HomeSpot(int32 NpcId) const;          // inside the NPC's house
    FIntVector DoorSpot(const FLisSite& Site) const; // just outside the door
    FIntVector WorkbenchSpot(int32 Slot = 0) const;  // in front of the bench, one place per slot
    FIntVector WorkbenchTarget() const;              // the bench block (to face)
    FIntVector StorageSpot() const;
    FIntVector StallSpot(bool bSeller, int32 Slot) const;
    FIntVector RestSpot(int32 Slot) const;
    FIntVector FireSpot() const;
    int32 HouseOf(int32 NpcId) const { return (NpcId - 1) / 4 % Houses; }

    // Statistics of the last Build calls.
    int32 Placed = 0;
    int32 Skipped = 0; // occupied by a block the village may not replace
    int32 TreesCleared = 0;
    bool ClearsTrees() const { return bClearTrees; }
    TArray<FAethraVillageOp> Ops;

    // Local (x, y) of a site to a world cell column, honouring its facing.
    static FIntPoint ToWorld(const FLisSite& Site, int32 X, int32 Y);
    static FIntPoint Size(EAethraSite Kind);

private:
    TArray<FLisSite> Sites;
    TMap<FIntVector, EAethraBlock> Expected; // village blocks (not the cleared cells)
    TArray<FIntVector> Crops;
    TArray<FIntVector> ClearedTrees;
    bool bClearTrees = false;
    int32 Next = 0;
    int32 Seed = 0;
    FIntVector Local(const FLisSite& Site, int32 X, int32 Y, int32 Z) const;
};
