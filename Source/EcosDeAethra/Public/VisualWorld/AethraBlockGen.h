// Aethra block world: the Visual World generators (WorldGen rivers, lakes, sea and biomes; AME soils;
// FloraGen trees and flowers; CaveGen caves; RuinGen ruins) turned into 1 m blocks for the voxel engine.
//
// The voxel engine asks AethraTerrain for columns by seed. Seeds with the Aethra flag (bit 30) are
// served by this generator; every other seed keeps the original voxel generator, so existing worlds
// and saves keep their meaning, and the block world saves under its own (flagged) seed.
//
// Heights: 1 block = 1 m up to 40 m above the sea, then compressed (hills x0.28, mountains x0.045)
// so the whole relief fits the 128-block column; sea level is block 12.
#pragma once

#include "CoreMinimal.h"
#include "AethraVoxelTypes.h"
#include "Containers/StaticArray.h"
#include "Misc/ScopeRWLock.h"
#include "VisualWorld/AethraFlora.h"
#include "VisualWorld/AethraWorldGen.h"

class FAethraCaveGen;
class FAethraRuinGen;
struct FRuinSite;

class ECOSDEAETHRA_API FAethraBlockGen
{
public:
    static constexpr int32 SeedFlag = 0x40000000;
    static constexpr int32 SeaLevel = 12;
    static constexpr int32 ColumnHeight = 128; // AethraTerrain::Height

    static bool IsAethraSeed(int32 Seed) { return Seed > 0 && (Seed & SeedFlag) != 0; }
    static int32 MakeSeed(int32 WorldSeed) { return SeedFlag | (WorldSeed & (SeedFlag - 1)); }
    static int32 WorldSeedOf(int32 Seed) { return Seed & (SeedFlag - 1); }
    // The generator of a flagged seed (built once, ~1 s, thread-safe).
    static const FAethraBlockGen& Get(int32 Seed);
    // World metres above the sea to a block height (fractional), and back (for depths: 1:1 below ground).
    static float MapHeight(float Metres);

    explicit FAethraBlockGen(int32 WorldSeed);
    ~FAethraBlockGen();

    int32 SurfaceAt(FIntPoint Point) const;
    void GenerateColumn(FIntPoint Point, TStaticArray<EAethraBlock, ColumnHeight>& Column) const;

    const FAethraWorldGen& World() const { return *Gen; }
    const FAethraCaveGen& Caves() const { return *CaveGen; }
    const FAethraRuinGen& Ruins() const { return *RuinGen; }
    // Spawn column near the origin: dry, gentle ground (block coordinates).
    FIntPoint SpawnPoint() const { return Spawn; }
    // Yaw (degrees) from the spawn towards the nearest water, so the first view shows the river.
    float SpawnYaw() const { return SpawnFacing; }

private:
    struct FColumnInfo
    {
        FWorldSample Sample;
        int32 Ground = 0;   // top solid block
        int32 WaterTop = -1; // top water block, -1 = none
        EAethraBlock Surface = EAethraBlock::Grass;
        EAethraBlock Subsoil = EAethraBlock::Dirt;
        EAethraBlock Rock = EAethraBlock::Stone;
    };
    struct FRuinCell { int16 Z; EAethraBlock Block; };

    TUniquePtr<FAethraWorldGen> Gen;
    TUniquePtr<FAethraCaveGen> CaveGen;
    TUniquePtr<FAethraRuinGen> RuinGen;
    TMap<FIntPoint, TArray<FRuinCell>> RuinColumns; // ruins and cave formations, rasterised once
    FIntPoint Spawn = FIntPoint::ZeroValue;
    float SpawnFacing = 70.f;

    mutable FRWLock CacheLock;
    mutable TMap<FIntPoint, FColumnInfo> ColumnCache;
    mutable TMap<FIntPoint, TOptional<FFloraTree>> TreeCache;

    FColumnInfo Column(FIntPoint Point) const;
    FColumnInfo ComputeColumn(FIntPoint Point) const;
    TOptional<FFloraTree> Tree(FIntPoint Cell) const;
    void Rasterise(const FRuinSite& Site);
    void AddCell(const FIntVector& Cell, EAethraBlock Block);
};
