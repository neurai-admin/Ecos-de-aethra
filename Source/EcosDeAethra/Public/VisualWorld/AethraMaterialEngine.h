// Aethra Material Engine (AME): a small library of original, parametric PBR surfaces and the rules that
// pick them from the environment. No texture files: every surface is a set of numbers (colours,
// roughness, a procedural pattern and its scale) that one shared material turns into pixels.
// Selection is deterministic: the same environment, seed and cell always give the same surface.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;

enum class EAmeCategory : uint8 { Terrain, Rock, Vegetation, Construction, Resource };

// Procedural pattern evaluated in the shader (M_AethraSurface). Values are stored in the palette.
enum class EAmePattern : uint8
{
    Noise, Grass, Meadow, Sand, Gravel, Strata, Speckle, Columns, Snow, Moss,
    Bark, Leaves, Planks, CutStone, Brick, Tiles, Plaster, Metal, Ore, Count
};

// Surface ids. New entries go at the end: ids are stored in vertex data and in saves.
enum class EAmeSurface : uint8
{
    Grass, Meadow, Dirt, Mud, Sand, Gravel,
    Granite, Basalt, Limestone, Sandstone, Marble, Slate, Snow,
    Moss, BarkOak, BarkBirch, BarkPine, LeavesBroad, LeavesNeedle, LeavesBirch, Reed,
    WoodPlank, CutStone, Brick, Mortar, RoofTile, Ceramic, Plaster, Iron, Bronze,
    OreCopper, OreIron, Crystal,
    Count
};

struct FAmeMaterialDef
{
    EAmeSurface Id = EAmeSurface::Grass;
    const TCHAR* Name = TEXT("");
    EAmeCategory Category = EAmeCategory::Terrain;
    FLinearColor Base;        // linear albedo
    FLinearColor Accent;      // second colour the pattern mixes in
    float Variation = .1f;    // large-scale colour drift 0..1
    float Roughness = .8f;
    float Metallic = 0.f;
    EAmePattern Pattern = EAmePattern::Noise;
    float PatternScale = 1.f; // pattern repeats per metre
    float Wear = 0.f;         // how much edges and exposure lighten/darken it 0..1
};

// Environment at a point, as the world generator sees it. Ranges are documented per field.
enum class EAmeBiome : uint8
{
    Plains, Meadow, Forest, Mountain, SnowPeak, RiverBank, Beach, Wetland,
    // Declared for the roadmap; the prototype generator does not produce them yet.
    Desert, Savanna, Tundra, Jungle, Volcanic, Cave, Count
};
enum class EAmeRock : uint8 { Granite, Basalt, Limestone, Sandstone, Slate, Marble, Count };
enum class EAmeCulture : uint8 { EldenStone, RiverTimber, HighlandSlate, Count };

struct FAmeEnvironment
{
    EAmeBiome Biome = EAmeBiome::Plains;
    EAmeRock Rock = EAmeRock::Granite;
    float Altitude = 0.f;      // metres above sea level
    float Moisture = .5f;      // 0 dry .. 1 waterlogged
    float Temperature = 15.f;  // degrees C
    float Slope = 0.f;         // 0 flat .. 1 vertical (1 - normal.z)
    float WaterDistance = 1e6f;// metres to the nearest water edge
    float Wetness = 0.f;       // weather 0..1 (rain darkens soil)
    float Wear = 0.f;          // exposure 0..1
};

struct FAmeChoice
{
    EAmeSurface Primary = EAmeSurface::Grass;
    EAmeSurface Secondary = EAmeSurface::Grass;
    float Blend = 0.f;         // 0 = all primary, 1 = all secondary
};

// Parts of a building the culture palettes cover.
enum class EAmePart : uint8 { Wall, Foundation, Roof, Trim, Column, Floor, Deck, Count };

class ECOSDEAETHRA_API FAmeRegistry
{
public:
    static const TArray<FAmeMaterialDef>& All();
    static const FAmeMaterialDef& Get(EAmeSurface Id);
    static const FAmeMaterialDef* FindByName(const FString& Name);
    // Every id once, in order, unique names, parameters in range. Returns the problems found.
    static TArray<FString> Validate();
    // Palette texture read by M_AethraSurface: 64 x 4 float texels, one column per surface.
    // Row 0 base rgb + variation, row 1 accent rgb + wear, row 2 roughness, metallic, pattern, scale.
    static UTexture2D* CreatePalette(UObject* Outer);
    static constexpr int32 PaletteWidth = 64;
    static constexpr int32 PaletteRows = 4;
};

class ECOSDEAETHRA_API FAmeSelector
{
public:
    // Terrain surface for an environment. Seed and cell only break ties and place patches; the
    // environment decides. Deterministic and independent of evaluation order.
    static FAmeChoice Terrain(const FAmeEnvironment& Env, int32 Seed, FIntPoint Cell);
    // Surface for a building part, from the culture, the local rock and the settlement's wealth (0..1).
    static EAmeSurface Building(EAmeCulture Culture, EAmePart Part, EAmeRock LocalRock, float Wealth);
    static EAmeSurface RockSurface(EAmeRock Rock);
    static const TCHAR* BiomeName(EAmeBiome Biome);
};
