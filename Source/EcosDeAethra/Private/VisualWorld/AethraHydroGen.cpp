#include "VisualWorld/AethraHydroGen.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"

namespace
{
// Per-metre coefficients (r, g, b), simplified from measured spectra and tuned for the look:
// pure water absorbs red most; dissolved organic matter and algae absorb blue; sediment absorbs a
// little across the board (brown) and scatters a lot; fine particles scatter almost white.
const FLinearColor PureWaterAbsorption(.42f, .065f, .028f);
const FLinearColor OrganicAbsorption(.08f, .22f, .62f);
const FLinearColor AlgaeAbsorption(.10f, .03f, .20f);
const FLinearColor SedimentAbsorption(.05f, .08f, .13f);
const FLinearColor PureWaterScattering(.002f, .004f, .009f);
const FLinearColor TurbidityScattering(.55f, .56f, .52f);
const FLinearColor AlgaeScattering(.02f, .07f, .03f);
const FLinearColor SedimentScattering(.45f, .38f, .26f);

FLinearColor Exp(const FLinearColor& C)
{
    return FLinearColor(FMath::Exp(C.R), FMath::Exp(C.G), FMath::Exp(C.B), 1.f);
}
}

float FHydroBody::Density() const
{
    // rho ~ 1000 * (1 - a (T - 4)^2) + b S, with a ~ 6.8e-6, b ~ 0.78 kg/m3 per g/kg.
    const float T = Props.Temperature;
    return 1000.f * (1.f - 6.8e-6f * FMath::Square(T - 4.f)) + .78f * Props.Salinity;
}

float FHydroBody::FloatFraction(float BodyDensity) const
{
    const float Water = Density();
    return BodyDensity >= Water ? 0.f : 1.f - BodyDensity / Water;
}

FHydroOptics FHydroGen::ComputeOptics(const FHydroProperties& P)
{
    FHydroOptics O;
    O.Absorption = PureWaterAbsorption + OrganicAbsorption * P.Organic + AlgaeAbsorption * P.Algae + SedimentAbsorption * P.Sediment;
    O.Scattering = PureWaterScattering + TurbidityScattering * P.Turbidity + AlgaeScattering * P.Algae + SedimentScattering * P.Sediment;
    // Single-scattering albedo per channel: the colour deep water returns when lit from above.
    // The 0.9 lifts it toward what a sky-lit surface shows (multiple scattering, sky light).
    const FLinearColor Extinction = O.Absorption + O.Scattering;
    O.InScatter = FLinearColor(O.Scattering.R / Extinction.R, O.Scattering.G / Extinction.G, O.Scattering.B / Extinction.B, 1.f);
    const float Peak = FMath::Max3(O.InScatter.R, O.InScatter.G, O.InScatter.B);
    // Water reflects little light back from its volume (a few percent when clear, ~15-25% when
    // full of silt); normalise so the hue survives and scale to that reflectance.
    const float Brightness = FMath::Clamp(.025f + .3f * Peak, .025f, .3f);
    O.InScatter = Peak > 0.f ? O.InScatter * (Brightness / Peak) : FLinearColor(0, 0, 0, 1);
    O.InScatter.A = 1.f;
    O.VisibleDepth = FMath::Loge(10.f) / FMath::Max(Extinction.G, 1e-3f);
    return O;
}

float FHydroGen::Transmittance(const FHydroOptics& O, float Depth)
{
    return FMath::Exp(-2.f * (O.Absorption.G + O.Scattering.G) * FMath::Max(Depth, 0.f));
}

FLinearColor FHydroGen::ApparentColor(const FHydroOptics& O, float Depth, const FLinearColor& Bottom)
{
    // Light goes down to the bottom and back: T = exp(-2 sigma d) per channel. What does not come
    // back from the bottom is replaced by light scattered within the water column.
    const FLinearColor Sigma = O.Absorption + O.Scattering;
    const FLinearColor T = Exp(Sigma * (-2.f * FMath::Max(Depth, 0.f)));
    FLinearColor C = Bottom * T + O.InScatter * (FLinearColor::White - T);
    C.A = 1.f;
    return C;
}

FHydroProperties FHydroGen::Preset(const TCHAR* Name)
{
    FHydroProperties P;
    const FString N(Name);
    if (N == TEXT("ClearStream"))      { P.Depth = .6f; P.Turbidity = .02f; P.Sediment = .01f; P.Organic = .02f; P.Algae = .02f; P.CurrentSpeed = 1.2f; P.Choppiness = .5f; }
    else if (N == TEXT("GreenRiver"))  { P.Depth = 3.f; P.Turbidity = .12f; P.Sediment = .08f; P.Organic = .12f; P.Algae = .55f; P.CurrentSpeed = .7f; P.Choppiness = .3f; }
    else if (N == TEXT("SiltRiver"))   { P.Depth = 3.f; P.Turbidity = .6f; P.Sediment = .9f; P.Organic = .1f; P.Algae = .1f; P.CurrentSpeed = 1.f; P.Choppiness = .35f; }
    else if (N == TEXT("DarkLake"))    { P.Depth = 9.f; P.Temperature = 10.f; P.Turbidity = .04f; P.Sediment = .02f; P.Organic = .7f; P.Algae = .15f; P.Choppiness = .15f; }
    else if (N == TEXT("ClearLake"))   { P.Depth = 7.f; P.Temperature = 8.f; P.Turbidity = .03f; P.Sediment = .01f; P.Organic = .04f; P.Algae = .05f; P.Choppiness = .15f; }
    else if (N == TEXT("TropicalSea")) { P.Depth = 12.f; P.Temperature = 26.f; P.Salinity = 35.f; P.Turbidity = .12f; P.Sediment = .0f; P.Organic = .0f; P.Algae = .02f; P.Choppiness = .5f; }
    else if (N == TEXT("DeepOcean"))   { P.Depth = 200.f; P.Temperature = 12.f; P.Salinity = 35.f; P.Turbidity = .01f; P.Organic = .0f; P.Algae = .03f; P.Choppiness = .7f; }
    else if (N == TEXT("SaltLake"))    { P.Depth = 2.f; P.Temperature = 20.f; P.Salinity = 120.f; P.Turbidity = .25f; P.Organic = .02f; P.Algae = .08f; P.Choppiness = .1f; }
    else if (N == TEXT("Waterfall"))   { P.Depth = .4f; P.Turbidity = .3f; P.Sediment = .02f; P.Organic = .03f; P.Algae = .02f; P.CurrentSpeed = 6.f; P.Choppiness = 1.f; }
    return P;
}

UTexture2D* FHydroGen::CreatePalette(const TArray<FHydroBody>& Bodies)
{
    UTexture2D* Texture = UTexture2D::CreateTransient(MaxBodies, 3, PF_A32B32G32R32F, TEXT("HydroPalette"));
    if (!Texture)
    {
        return nullptr;
    }
    Texture->Filter = TF_Nearest;
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_HDR;
    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    FLinearColor* Texels = static_cast<FLinearColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memzero(Texels, sizeof(FLinearColor) * MaxBodies * 3);
    for (const FHydroBody& B : Bodies)
    {
        if (B.Id < 0 || B.Id >= MaxBodies)
        {
            continue;
        }
        const FHydroOptics& O = B.Optics;
        Texels[0 * MaxBodies + B.Id] = FLinearColor(O.Absorption.R, O.Absorption.G, O.Absorption.B, B.Props.CurrentSpeed);
        Texels[1 * MaxBodies + B.Id] = FLinearColor(O.Scattering.R, O.Scattering.G, O.Scattering.B, B.Props.Choppiness);
        Texels[2 * MaxBodies + B.Id] = FLinearColor(O.InScatter.R, O.InScatter.G, O.InScatter.B, O.VisibleDepth);
    }
    Mip.BulkData.Unlock();
    Texture->UpdateResource();
    return Texture;
}
