// Aethra WorldGen: the landscape generator of the Visual World (prototype 01).
//
// It is separate from AethraTerrain (the voxel world's generator), so existing seeds and saves keep
// their meaning. Everything is a pure function of the seed and the position:
//   * climate fields (temperature with a stylised lapse rate, moisture, rock provinces);
//   * relief: wide plains, soft hills and a mountain range that rises away from the spawn basin;
//   * hydrology on a 16 m drainage grid over 8 x 8 km: depressions are filled (priority flood),
//     flow goes downhill (D8), accumulation makes streams and rivers, filled depressions become
//     lakes and low continental ground becomes sea. Any point samples the same grid, so rivers are
//     continuous across chunks and regions by construction;
//   * biomes blended from climate, altitude, slope and water, not a mosaic of random patches;
//   * waterfall sites where a river drops steeply, and flattened pads for settlements.
#pragma once

#include "CoreMinimal.h"
#include "VisualWorld/AethraHydroGen.h"
#include "VisualWorld/AethraMaterialEngine.h"

struct FWorldSample
{
    float Height = 0.f;       // terrain surface, metres
    float WaterLevel = -1e9f; // water surface, metres (below Height = dry)
    int32 WaterBody = -1;     // FAethraWorldGen::Bodies index, -1 = none
    float WaterDistance = 1e6f; // metres to the nearest water edge (approximate)
    float RiverStrength = 0.f;  // 0 none .. 1 big river
    FVector2f Flow = FVector2f::ZeroVector; // downstream direction times speed (m/s)
    float Temperature = 15.f;
    float Moisture = .5f;
    EAmeBiome Biome = EAmeBiome::Plains;
    EAmeRock Rock = EAmeRock::Granite;
    float ForestDensity = 0.f; // 0..1
    bool IsWater() const { return WaterLevel > Height + .05f; }
};

struct FWaterfallSite
{
    FVector2D Position = FVector2D::ZeroVector; // metres, top of the fall, where the river is drawn (warped)
    FVector2D Direction = FVector2D(1, 0);      // downstream
    float Top = 0.f, Bottom = 0.f, Width = 3.f; // metres
    float Length = 16.f;                         // metres from the top to the foot (one drainage cell)
    int32 Body = -1;
};

struct FSettlementPad
{
    FVector2D Center = FVector2D::ZeroVector;
    float Radius = 20.f; // metres of flat ground
    float Height = 0.f;
};

class ECOSDEAETHRA_API FAethraWorldGen
{
public:
    static constexpr double DomainHalf = 4096.0; // metres: the drainage grid covers +-4 km
    static constexpr double NodeSpacing = 16.0;  // metres between drainage nodes
    static constexpr int32 Nodes = 513;

    explicit FAethraWorldGen(int32 InSeed = 1);
    int32 GetSeed() const { return Seed; }

    // Relief before rivers and pads (metres). Pure function of the seed.
    float BaseHeight(FVector2D P) const;
    // Everything at a point, with rivers carved, lakes/sea filled and pads flattened.
    FWorldSample Sample(FVector2D P) const;
    // Height only (cheaper than Sample when water and biome are not needed).
    float Height(FVector2D P) const { return Sample(P).Height; }

    const TArray<FHydroBody>& GetBodies() const { return Bodies; }
    const TArray<FWaterfallSite>& GetWaterfalls() const { return Waterfalls; }
    const TArray<FSettlementPad>& GetPads() const { return Pads; }
    // Adds a flat pad (settlement planning). Call before generating chunks.
    void AddPad(const FSettlementPad& Pad) { Pads.Add(Pad); }

    // Drainage statistics for reports and tests.
    int32 RiverNodes = 0, LakeNodes = 0, SeaNodes = 0;
    float MaxAccumulation = 0.f;
    double InitMs = 0.0;
    // The node index of the strongest river's mouth and the path to it from its source (node coords).
    TArray<FIntPoint> MainRiver;

    // Grid access (tests and tools).
    bool NodeInDomain(FIntPoint N) const { return N.X >= 0 && N.Y >= 0 && N.X < Nodes && N.Y < Nodes; }
    FVector2D NodePosition(FIntPoint N) const { return FVector2D(N.X * NodeSpacing - DomainHalf, N.Y * NodeSpacing - DomainHalf); }
    float NodeAccumulation(FIntPoint N) const { return Accum[N.Y * Nodes + N.X]; }
    int32 NodeDownstream(FIntPoint N) const { return Down[N.Y * Nodes + N.X]; }
    float NodeLevel(FIntPoint N) const { return Filled[N.Y * Nodes + N.X]; }
    // Rivers are drawn through a domain warp: where a grid point appears in the world (approximate).
    FVector2D WorldOfGridPoint(FVector2D R) const;

private:
    int32 Seed = 1;
    FVector2D Offsets[12];
    TArray<float> Base, Filled, Accum;
    TArray<int32> Down;
    // Nearest-river data per node (multi-source distance transform).
    TArray<float> RiverDist, RiverLevel, RiverWidth, RiverDepth, RiverPower, LakeMask;
    TArray<int32> RiverBodyAt, LakeBody;
    TArray<FVector2f> RiverDir;
    TArray<FHydroBody> Bodies;
    TArray<FWaterfallSite> Waterfalls;
    TArray<FSettlementPad> Pads;

    float Noise(FVector2D P) const;                 // gradient noise in [-1, 1]
    FVector2D RiverLookup(FVector2D P) const;       // where world point P reads the drainage grid (domain warp)
    float Fbm(FVector2D P, int32 Octaves) const;
    float Ridged(FVector2D P, int32 Octaves) const; // 0..1, sharp crests
    void BuildDrainage();
    float Bilinear(const TArray<float>& Field, FVector2D P) const;
    int32 NearestNode(FVector2D P) const;
};
