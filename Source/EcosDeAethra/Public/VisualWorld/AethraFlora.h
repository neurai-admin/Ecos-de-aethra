// Aethra Flora: the procedural species system of the Visual World.
//
// Art direction (vegetation addendum): common trees keep a geometric identity built from cubes,
// squares and rectangles; pines use triangular silhouettes and stylised conical (here pyramidal)
// tiers; palms and other species use their own angular forms; only rare, ancient or special trees
// may be organic and rounded. Materials stay rich (AME bark and leaf surfaces with procedural detail).
//
// A species is data: form, trunk, crown, AME surfaces, size range, wind, biome weights, rarity and
// draw distance. One mesh per species is built from those numbers at startup. Placement is a pure
// function of the seed and a 7 m grid cell, so every tree has a stable id (the cell) at every level of
// detail and in every session; FFloraLayer persists the trees that were felled (by the player or,
// later, by NPCs) in its own file, so a felled tree stays felled after a restart.
#pragma once

#include "CoreMinimal.h"
#include "VisualWorld/AethraMaterialEngine.h"

class FAethraWorldGen;
struct FAhgMeshBuilder;

enum class EFloraForm : uint8
{
    Blocky,  // common broadleaf trees and bushes: stacked boxes
    Conical, // conifers: tiers with a triangular silhouette (square pyramids)
    Angular, // palms and other species with their own angular forms
    Organic, // rare, ancient or special trees: rounded, irregular, more complex
};

// Species ids are stored in tree ids and saves: new species go at the end.
enum class EFloraSpecies : uint8
{
    Oak, Birch, Pine, Bush, Willow, Palm, Ancient,
    Count
};

struct FFloraSpecies
{
    EFloraSpecies Id = EFloraSpecies::Oak;
    const TCHAR* Name = TEXT("");
    EFloraForm Form = EFloraForm::Blocky;
    EAmeSurface Bark = EAmeSurface::BarkOak;
    EAmeSurface Leaves = EAmeSurface::LeavesBroad;
    float TrunkHeight = 4.f, TrunkWidth = .6f;  // metres
    float CrownWidth = 5.f, CrownHeight = 4.f;  // metres
    int32 CrownTiers = 2;                       // boxes, pyramids or fronds, by form
    float ScaleMin = .8f, ScaleMax = 1.25f;     // uniform size variation per tree
    float Wind = .5f;                           // sway at the crown top, 0 rigid .. 1 supple
    float BiomeWeight[static_cast<int32>(EAmeBiome::Count)] = {}; // relative frequency per biome
    float MinTemperature = -50.f, MaxTemperature = 50.f;          // degrees C
    float DrawDistance = 600.f;                 // metres; also the shadow-casting LOD limit
    bool bFar = true;                           // kept in far (level 1) chunks
    bool IsCommon() const { return Form != EFloraForm::Organic; }
};

struct FFloraTree
{
    uint64 Id = 0;          // stable: grid cell and species
    EFloraSpecies Species = EFloraSpecies::Oak;
    FVector2D Position = FVector2D::ZeroVector; // metres
    float Ground = 0.f;     // metres
    float Scale = 1.f, Yaw = 0.f, Tint = 0.f;
};

class ECOSDEAETHRA_API FFloraRegistry
{
public:
    static const TArray<FFloraSpecies>& All();
    static const FFloraSpecies& Get(EFloraSpecies Id);
    // Every id once and in order, ranges valid, common trees not organic, a weight somewhere.
    static TArray<FString> Validate();
    // Species for a place, or -1: weighted by biome and limited by temperature. Roll in [0, 1).
    static int32 Pick(EAmeBiome Biome, float Temperature, float Roll);
    // The species mesh at unit scale (centimetres, base at z = 0), AME surfaces in UV1, and UV0.y the
    // height fraction the wind uses (0 at the ground, 1 at the crown top).
    static void BuildMesh(const FFloraSpecies& Species, int32 Seed, FAhgMeshBuilder& Out);
    // Height of the species mesh at scale 1 (centimetres).
    static float MeshHeight(const FFloraSpecies& Species) { return (Species.TrunkHeight + Species.CrownHeight) * 100.f; }
};

class ECOSDEAETHRA_API FFloraPlacer
{
public:
    static constexpr double Cell = 7.0; // metres per placement cell (one tree at most)
    // The tree of a cell, if any. Pure function of the generator (seed) and the cell; settlement and
    // water exclusions come from the generator's samples and pads.
    static bool TreeAt(const FAethraWorldGen& Gen, FIntPoint Cell, FFloraTree& Out);
    static uint64 MakeId(FIntPoint Cell, EFloraSpecies Species);
    static FIntPoint CellOf(uint64 Id);
};

// Trees removed from the generated forest. Own versioned file; the voxel save is untouched.
class ECOSDEAETHRA_API FFloraLayer
{
public:
    static constexpr uint32 Magic = 0x31524C46; // "FLR1"
    static constexpr int32 Version = 1;

    bool Fell(uint64 TreeId) { bool bAlready = false; Felled.Add(TreeId, &bAlready); return !bAlready; }
    bool Restore(uint64 TreeId) { return Felled.Remove(TreeId) > 0; }
    bool IsFelled(uint64 TreeId) const { return Felled.Contains(TreeId); }
    int32 Num() const { return Felled.Num(); }

    void Serialize(FArchive& Ar);
    bool SaveToFile(const FString& Path) const;
    bool LoadFromFile(const FString& Path);

private:
    TSet<uint64> Felled;
};
