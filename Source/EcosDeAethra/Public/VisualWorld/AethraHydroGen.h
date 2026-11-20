// Aethra HydroGen: properties of each body of water and the optics they produce.
// Colour is never picked from a table by kind or salinity: it comes from absorption and scattering
// per colour channel (pure water, dissolved organic matter, algae, suspended sediment), the way
// real water gets its tint. Salinity changes density (and, later, buoyancy), not colour.
// What is simulated: optics per body, depth-dependent transmittance, flow speed for the surface.
// What is not: fluid dynamics. Rivers and waterfalls do not move water; their surfaces are animated.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;

enum class EHydroKind : uint8 { River, Stream, Lake, Sea, SaltLake, Waterfall, Count };

struct FHydroProperties
{
    float Depth = 2.f;          // typical depth, metres
    float Temperature = 14.f;   // degrees C
    float Salinity = 0.f;       // g/kg (fresh < 0.5, brackish 0.5-30, sea ~35, salt lakes > 50)
    float Turbidity = .05f;     // fine suspended particles that scatter light, 0..2
    float Sediment = .05f;      // coarser silt that also absorbs (brown), 0..2
    float Organic = .05f;       // dissolved organic matter (tannins), absorbs blue, 0..2
    float Algae = .05f;         // phytoplankton: green tint, 0..2
    float CurrentSpeed = 0.f;   // m/s, drives the surface animation and foam
    float Choppiness = .3f;     // surface wave strength 0..1
};

struct FHydroOptics
{
    FLinearColor Absorption;    // per metre, rgb
    FLinearColor Scattering;    // per metre, rgb
    FLinearColor InScatter;     // colour of deep water lit from above, rgb 0..1
    float VisibleDepth = 5.f;   // metres at which green light falls to 10%
};

struct FHydroBody
{
    int32 Id = 0;
    FString Name;
    EHydroKind Kind = EHydroKind::River;
    FHydroProperties Props;
    FHydroOptics Optics;
    bool IsSalty() const { return Props.Salinity >= .5f; }
    // kg/m3 from a linearised equation of state (good to ~1% for 0-40 C, 0-40 g/kg).
    float Density() const;
    // Fraction of a body of density Body that floats above this water (0 = sinks), for future buoyancy.
    float FloatFraction(float BodyDensity) const;
};

class ECOSDEAETHRA_API FHydroGen
{
public:
    static FHydroOptics ComputeOptics(const FHydroProperties& Props);
    // Colour seen from above through Depth metres of water over a bottom of albedo Bottom (sunlit).
    static FLinearColor ApparentColor(const FHydroOptics& Optics, float Depth, const FLinearColor& Bottom);
    // Transmittance of the green channel down and back up through Depth metres.
    static float Transmittance(const FHydroOptics& Optics, float Depth);
    // Named presets used by the world generator. They are starting properties, not colours.
    static FHydroProperties Preset(const TCHAR* Name);
    // Water palette for M_AethraHydro: 16 x 3 float texels, one column per body.
    // Row 0 absorption rgb + current speed, row 1 scattering rgb + choppiness, row 2 in-scatter rgb + visible depth.
    static UTexture2D* CreatePalette(const TArray<FHydroBody>& Bodies);
    static constexpr int32 MaxBodies = 16;
};
