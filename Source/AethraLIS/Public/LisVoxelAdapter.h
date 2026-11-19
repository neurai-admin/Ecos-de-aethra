// Bridge from voxel block changes to LIS events. The LIS module does not depend on
// the voxel runtime: the integrator forwards AAethraVoxelWorld::OnBlockChanged here
// (cell, block before, block after as uint8) and supplies the block classifier.
#pragma once

#include "LisTypes.h"

class FLisSimulation;

enum class ELisVoxelResource : uint8 { None, Wood, Foliage, Stone, Soil, Water, Crop };

struct FLisVoxelChange
{
    FIntVector Cell = FIntVector::ZeroValue;
    uint8 Before = 0;
    uint8 After = 0;
    int32 Actor = LisNobody; // the current voxel event has no author; LisPlayer when the integrator knows
};

class AETHRALIS_API FLisVoxelAdapter
{
public:
    // Maps a block id to the resource it represents. Required before Flush.
    TFunction<ELisVoxelResource(uint8)> Classify;
    // A change belongs to the nearest place anchor within this many cells; others are ignored.
    float PlaceRadiusCells = 64.f;
    // Removed blocks per unit of abstract stock (a tree is several wood blocks).
    float BlocksPerUnit = 4.f;

    // Cheap: called from the game thread for every accepted block change.
    void OnBlockChanged(const FLisVoxelChange& Change) { Pending.Add(Change); }
    int32 PendingCount() const { return Pending.Num(); }

    // Aggregates pending changes per place, resource and kind into LIS events,
    // adjusts the abstract stock and lets only nearby NPCs perceive them.
    // Returns the number of LIS events emitted.
    int32 Flush(FLisSimulation& Sim);

    int32 Ignored = 0;

private:
    TArray<FLisVoxelChange> Pending;
};
