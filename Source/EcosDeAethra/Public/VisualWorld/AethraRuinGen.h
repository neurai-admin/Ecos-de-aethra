// Aethra RuinGen: ruins of ancient civilisations, on the surface and underground.
//
// A ruin site is planned from the place (biome, altitude, water, depth, rock) and the seed: a kind
// (temple, broken wall, fallen tower, shrine, statue remains; underground: hall, crypt, abandoned mine),
// an ancient culture of Aethra with its own materials, a state of preservation, an age, an importance
// and a rarity. Build() turns a site into AHG pieces (the same hybrid shapes and AME surfaces as every
// construction) with decay: missing upper parts, fallen and tilted pieces, moss on the top faces.
//
// Not decoration only: every site carries lore tags and a cache point (where a reward goes), the
// contracts for exploration, lore and rewards. Loot, texts and quests are future systems.
// Ruins are real and reachable: they stand on the WorldGen surface (or a cave floor) and are drawn at
// every distance, so what the horizon shows is what the player finds.
#pragma once

#include "CoreMinimal.h"
#include "VisualWorld/AethraHybridGeometry.h"
#include "VisualWorld/AethraMaterialEngine.h"

class FAethraWorldGen;
class FAethraCaveGen;

enum class ERuinKind : uint8
{
    Temple, BrokenWall, FallenTower, Shrine, StatueRemains, // surface
    CaveHall, Crypt, AbandonedMine,                         // underground
    Count
};

// Ancient cultures of Aethra (original): what they built with and how.
enum class ERuinCulture : uint8
{
    Aldarim, // white limestone and marble temples, columns, domes
    Skarn,   // dark basalt and slate fortresses, iron fittings
    Mirethi, // warm sandstone shrines with blue ceramic
    Count
};

struct FRuinPiece
{
    EAhgShape Shape = EAhgShape::Block;
    EAmeSurface Surface = EAmeSurface::CutStone;
    FTransform Transform; // centimetres; scale = size in metres (the shapes are 1 m units)
};

struct FRuinSite
{
    int32 Id = 0;
    ERuinKind Kind = ERuinKind::Temple;
    ERuinCulture Culture = ERuinCulture::Aldarim;
    FVector Anchor = FVector::ZeroVector; // metres: ground (or cave floor) at the centre
    float Yaw = 0.f;
    float Radius = 12.f;                  // metres of footprint (trees keep out)
    float Preservation = .5f;             // 0 rubble .. 1 nearly intact
    float Importance = .5f;               // 0..1
    float AgeYears = 1000.f;
    int32 Rarity = 0;                     // 0 common, 1 uncommon, 2 rare, 3 legendary
    bool bUnderground = false;
    int32 CaveSystem = -1;
    FVector Cache = FVector::ZeroVector;  // metres: where a reward will be placed (contract)
    TArray<FName> Lore;                   // tags for the future lore system
};

class ECOSDEAETHRA_API FAethraRuinGen
{
public:
    // Plans the ruins of the world: a demonstration temple near Spawn, a demonstration hall in the demo
    // cave, and sparse sites across the domain. Pure function of the generator and the caves.
    FAethraRuinGen(const FAethraWorldGen& Gen, const FAethraCaveGen* Caves, const FVector2D& Spawn);

    const TArray<FRuinSite>& Sites() const { return RuinSites; }
    // The ruin whose footprint holds P (metres, surface ruins only), if any.
    const FRuinSite* SurfaceSiteAt(const FVector2D& P) const;
    static void Build(const FRuinSite& Site, int32 Seed, TArray<FRuinPiece>& Out);
    static const TCHAR* KindName(ERuinKind Kind);
    static const TCHAR* CultureName(ERuinCulture Culture);

private:
    TArray<FRuinSite> RuinSites;
    int32 Seed = 1;
};
