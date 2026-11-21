// Aethra FloraGen: the procedural vegetation of the Visual World (trees, ground flora, flower fields).
//
// Art direction (Prompt 08 and the vegetation addendum): common trees keep a geometric identity built
// from cubes, squares and rectangles, with segmented trunks and angular branches; pines use triangular
// silhouettes and stylised pyramidal tiers; palms, dry trees and other species use their own angular
// forms; only rare, ancient or special trees may be organic and rounded. Materials stay rich (AME
// surfaces with procedural detail and bump): nothing looks like plasticine.
//
// Everything is data plus pure functions of the seed and the position:
//   * FFloraEnvironment: biome, temperature, moisture, altitude, slope, sun exposure, fertility, water
//     availability and rock - what decides where a plant grows (the season is reserved for later);
//   * trees on a 7 m grid: species weighted by biome, climate and ecology, with regional groves,
//     forest clearings and open flower fields; three shape variants per species;
//   * ground flora on a 1.7 m grid: grass, ferns and flowers; flower fields (sunflower, wild rose,
//     lavender, mixed wildflowers, alpine) are large regions chosen by low-frequency noise where the
//     ecology suits them, and tint the terrain from far away so they read at every distance;
//   * resource contracts (FFloraUse) for future harvesting, farming, dyes, perfume, alchemy and
//     pollination - declared, not implemented as mechanics yet.
// Every tree has a stable id (its cell); FFloraLayer persists felled trees in its own file.
#pragma once

#include "CoreMinimal.h"
#include "VisualWorld/AethraMaterialEngine.h"

class FAethraWorldGen;
struct FAhgMeshBuilder;
struct FWorldSample;

enum class EFloraForm : uint8
{
    Blocky,  // common broadleaf trees and bushes: boxes, segmented trunks, angular branches
    Conical, // conifers: tiers with a triangular silhouette (square pyramids)
    Angular, // palms, dry trees and other species with their own angular forms
    Organic, // rare, ancient or special trees: rounded, irregular, more complex
};

// Species ids are stored in tree ids and saves: new species go at the end.
enum class EFloraSpecies : uint8
{
    Oak, Birch, Pine, Bush, Willow, Palm, Ancient, DeadTree,
    Count
};

// What a plant can give, for future systems (declared now so data and saves can carry it).
enum class EFloraUse : uint16
{
    None = 0, Harvest = 1 << 0, Seeds = 1 << 1, Dye = 1 << 2, Perfume = 1 << 3, Alchemy = 1 << 4,
    Pollinator = 1 << 5, Ornamental = 1 << 6, Food = 1 << 7, Fiber = 1 << 8, Wood = 1 << 9,
};
ENUM_CLASS_FLAGS(EFloraUse);

// Ecology at a point.
struct ECOSDEAETHRA_API FFloraEnvironment
{
    EAmeBiome Biome = EAmeBiome::Plains;
    EAmeRock Rock = EAmeRock::Granite;
    float Temperature = 15.f;  // degrees C
    float Moisture = .5f;      // 0..1
    float Altitude = 0.f;      // metres
    float Slope = 0.f;         // 0 flat .. 1 vertical
    float Sun = .7f;           // exposure 0 (steep, facing away from the sun) .. 1 (facing it)
    float Fertility = .5f;     // 0..1 from rock, moisture, slope and nearby rivers
    float Water = .5f;         // availability 0..1 (moisture and distance to water)
    float ForestDensity = 0.f; // the generator's forest field 0..1
    float Season = 0.f;        // reserved: no season system yet (0 = summer)
    bool bWater = false;       // under water
    static FFloraEnvironment From(const FWorldSample& Sample, const FVector3f& Normal);
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
    float MinMoisture = 0.f, MaxMoisture = 1.f;                   // soft limits (ecology)
    float DrawDistance = 600.f;                 // metres; also the shadow-casting LOD limit
    bool bFar = true;                           // kept in far (level 1) chunks
    EFloraUse Uses = EFloraUse::Wood;
    bool IsCommon() const { return Form != EFloraForm::Organic; }
};

struct FFloraTree
{
    uint64 Id = 0;          // stable: grid cell and species
    EFloraSpecies Species = EFloraSpecies::Oak;
    int32 Variant = 0;      // 0..FFloraRegistry::Variants-1
    FVector2D Position = FVector2D::ZeroVector; // metres
    float Ground = 0.f;     // metres
    float Scale = 1.f, Yaw = 0.f, Tint = 0.f;
};

class ECOSDEAETHRA_API FFloraRegistry
{
public:
    static constexpr int32 Variants = 3; // shape variants per tree species
    static const TArray<FFloraSpecies>& All();
    static const FFloraSpecies& Get(EFloraSpecies Id);
    // Every id once and in order, ranges valid, common trees not organic, a weight somewhere.
    static TArray<FString> Validate();
    // Species for a biome and temperature, or -1 (biome weights only). Roll in [0, 1).
    static int32 Pick(EAmeBiome Biome, float Temperature, float Roll);
    // Species for a place: biome weights times ecology (moisture) times regional groves. Roll in [0, 1).
    static int32 PickAt(const FFloraEnvironment& Env, int32 Seed, FVector2D P, float Roll);
    // The species mesh (variant 0..Variants-1) at unit scale (centimetres, base at z = 0), AME surfaces in
    // UV1, and UV0.y the height fraction the wind uses (0 at the ground, 1 at the crown top).
    static void BuildMesh(const FFloraSpecies& Species, int32 Seed, int32 Variant, FAhgMeshBuilder& Out);
    static float MeshHeight(const FFloraSpecies& Species) { return (Species.TrunkHeight + Species.CrownHeight) * 100.f; }
};

// ---------------------------------------------------------------- ground flora and flower fields

enum class EGroundSpecies : uint8
{
    Grass, Fern, Sunflower, WildRose, Lavender, Daisy, Poppy, Tulip, AlpineFlower,
    Count
};

enum class EFlowerField : uint8 { None, Sunflower, WildRose, Lavender, Wildflower, Alpine, Count };

struct FGroundSpecies
{
    EGroundSpecies Id = EGroundSpecies::Grass;
    const TCHAR* Name = TEXT("");
    float HeightMin = .3f, HeightMax = .5f; // metres (the mesh is built at HeightMax)
    float Wind = .8f;
    float DrawDistance = 120.f;             // metres (scaled by the graphics profile)
    EFloraUse Uses = EFloraUse::None;
    FLinearColor DyeColor = FLinearColor::Transparent; // for EFloraUse::Dye
    bool bFoliageMaterial = false;          // grass blades: the light two-sided foliage material
};

struct FGroundPlant
{
    EGroundSpecies Species = EGroundSpecies::Grass;
    FVector2D Position = FVector2D::ZeroVector;
    float Scale = 1.f, Yaw = 0.f, Tint = 0.f;
};

struct FFieldSample
{
    EFlowerField Field = EFlowerField::None;
    float Strength = 0.f; // 0..1: how far inside the field (edges fade out)
};

class ECOSDEAETHRA_API FFloraGen
{
public:
    static constexpr double GroundCell = 1.7; // metres per ground-flora cell
    static const TArray<FGroundSpecies>& Ground();
    static const FGroundSpecies& Get(EGroundSpecies Id);
    // Smooth value noise in [0, 1] with features of about Scale metres.
    static float Noise(int32 Seed, FVector2D P, double Scale, uint32 Salt);
    // The flower field at a point, if any: large regions where the ecology suits the field's flowers.
    static FFieldSample FieldAt(int32 Seed, FVector2D P, const FFloraEnvironment& Env);
    // What a field looks like from far away (linear albedo), for the terrain tint at every LOD.
    static FLinearColor FieldColor(EFlowerField Field);
    static const TCHAR* FieldName(EFlowerField Field);
    // Jittered position of a ground cell.
    static FVector2D GroundPosition(int32 Seed, FIntPoint Cell);
    // The ground plant of a cell (Env at GroundPosition), if any.
    static bool GroundAt(int32 Seed, FIntPoint Cell, const FFloraEnvironment& Env, FGroundPlant& Out);
    // Mesh of a ground species at full height (centimetres, base at z = 0), UV0.y = height fraction; grass
    // blades carry the wind weight in vertex colour R (foliage material).
    static void BuildMesh(const FGroundSpecies& Species, int32 Seed, FAhgMeshBuilder& Out);
};

class ECOSDEAETHRA_API FFloraPlacer
{
public:
    static constexpr double Cell = 7.0; // metres per placement cell (one tree at most)
    // The tree of a cell, if any. Pure function of the generator (seed) and the cell; water, pads,
    // steep ground, forest clearings and flower fields keep it out.
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
