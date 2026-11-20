#include "VisualWorld/AethraFlora.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "VisualWorld/AethraHybridGeometry.h"
#include "VisualWorld/AethraWorldGen.h"

// Names in this file are prefixed (Flora...) because the unity build merges it with other files.
namespace
{
using FB = EAmeBiome;
using FS = EAmeSurface;

uint32 FloraMix(uint32 H)
{
    H ^= H >> 16;
    H *= 0x7feb352dU;
    H ^= H >> 15;
    H *= 0x846ca68bU;
    H ^= H >> 16;
    return H;
}

float FloraHash01(int32 Seed, int32 X, int32 Y, uint32 Salt)
{
    return (FloraMix(static_cast<uint32>(Seed) ^ FloraMix(static_cast<uint32>(X) * 0x9E3779B1U ^ FloraMix(static_cast<uint32>(Y) * 0x85EBCA77U + Salt))) & 0xFFFFFF) / 16777215.f;
}

float FloraSurface(EAmeSurface S)
{
    return static_cast<float>(S);
}

// Soft window: 1 inside [Lo, Hi], fading to 0 over Soft outside it.
float FloraWindow(float V, float Lo, float Hi, float Soft)
{
    return FMath::Clamp(FMath::Min((V - Lo) / Soft + 1.f, (Hi - V) / Soft + 1.f), 0.f, 1.f);
}

TArray<FFloraSpecies> FloraMakeSpecies()
{
    TArray<FFloraSpecies> L;
    auto Add = [&L](EFloraSpecies Id, const TCHAR* Name, EFloraForm Form, EAmeSurface Bark, EAmeSurface Leaves,
        float TrunkH, float TrunkW, float CrownW, float CrownH, int32 Tiers, float SMin, float SMax, float Wind,
        float MinT, float MaxT, float Draw, bool bFar, std::initializer_list<TPair<FB, float>> Weights) -> FFloraSpecies&
    {
        FFloraSpecies& S = L.AddDefaulted_GetRef();
        S.Id = Id; S.Name = Name; S.Form = Form; S.Bark = Bark; S.Leaves = Leaves;
        S.TrunkHeight = TrunkH; S.TrunkWidth = TrunkW; S.CrownWidth = CrownW; S.CrownHeight = CrownH; S.CrownTiers = Tiers;
        S.ScaleMin = SMin; S.ScaleMax = SMax; S.Wind = Wind; S.MinTemperature = MinT; S.MaxTemperature = MaxT;
        S.DrawDistance = Draw; S.bFar = bFar;
        for (const TPair<FB, float>& W : Weights)
        {
            S.BiomeWeight[static_cast<int32>(W.Key)] = W.Value;
        }
        return S;
    };
    using F = EFloraForm;
    Add(EFloraSpecies::Oak, TEXT("Carvalho-de-Elden"), F::Blocky, FS::BarkOak, FS::LeavesBroad, 3.2f, .8f, 6.f, 4.4f, 3, .8f, 1.25f, .45f, 4.f, 40.f, 2000.f, true,
        { { FB::Plains, 1.f }, { FB::Meadow, 1.f }, { FB::Forest, .55f }, { FB::RiverBank, .45f }, { FB::Wetland, .2f } }).MinMoisture = .25f;
    Add(EFloraSpecies::Birch, TEXT("Bétula-Clara"), F::Blocky, FS::BarkBirch, FS::LeavesBirch, 5.2f, .4f, 2.8f, 4.2f, 3, .85f, 1.2f, .7f, -4.f, 30.f, 1600.f, true,
        { { FB::Plains, .3f }, { FB::Meadow, .3f }, { FB::Forest, .25f }, { FB::RiverBank, .25f }, { FB::Mountain, .15f } }).MinMoisture = .35f;
    Add(EFloraSpecies::Pine, TEXT("Pinheiro-Escalonado"), F::Conical, FS::BarkPine, FS::LeavesNeedle, 1.6f, .6f, 5.2f, 7.4f, 5, .8f, 1.35f, .3f, -20.f, 22.f, 2000.f, true,
        { { FB::Mountain, 1.f }, { FB::Forest, .12f }, { FB::SnowPeak, .25f }, { FB::Plains, .03f } });
    Add(EFloraSpecies::Bush, TEXT("Arbusto-de-Blocos"), F::Blocky, FS::BarkOak, FS::LeavesBroad, 0.f, 0.f, 2.4f, 1.4f, 3, .7f, 1.3f, .8f, 0.f, 40.f, 450.f, false,
        { { FB::Plains, .25f }, { FB::Meadow, .3f }, { FB::Forest, .1f }, { FB::RiverBank, .2f }, { FB::Wetland, .3f } });
    FFloraSpecies& Willow = Add(EFloraSpecies::Willow, TEXT("Salgueiro-do-Rio"), F::Blocky, FS::BarkOak, FS::LeavesBirch, 3.4f, .7f, 6.4f, 3.2f, 2, .85f, 1.2f, .9f, 5.f, 35.f, 1600.f, true,
        { { FB::RiverBank, .6f }, { FB::Wetland, .8f } });
    Willow.MinMoisture = .55f;
    Add(EFloraSpecies::Palm, TEXT("Palmeira-Angular"), F::Angular, FS::BarkPine, FS::LeavesBroad, 6.5f, .45f, 6.f, 1.2f, 7, .85f, 1.2f, .85f, 8.f, 45.f, 1600.f, true,
        { { FB::Beach, 1.f } }).Uses = EFloraUse::Wood | EFloraUse::Food | EFloraUse::Fiber;
    Add(EFloraSpecies::Ancient, TEXT("Árvore Anciã"), F::Organic, FS::BarkOak, FS::LeavesBroad, 5.f, 2.2f, 13.f, 9.f, 9, .9f, 1.15f, .2f, 5.f, 35.f, 4000.f, true,
        { { FB::Forest, .003f }, { FB::Meadow, .004f }, { FB::Plains, .002f } }).Uses = EFloraUse::Wood | EFloraUse::Alchemy; // rare: ~1 in 300
    FFloraSpecies& Dead = Add(EFloraSpecies::DeadTree, TEXT("Árvore-Seca"), F::Angular, FS::BarkPine, FS::BarkPine, 3.6f, .45f, 4.f, 2.2f, 5, .8f, 1.3f, .15f, -15.f, 40.f, 1200.f, true,
        { { FB::Plains, .04f }, { FB::Mountain, .1f }, { FB::SnowPeak, .12f } });
    Dead.MaxMoisture = .42f; // only where it is dry
    return L;
}

TArray<FGroundSpecies> FloraMakeGround()
{
    TArray<FGroundSpecies> L;
    auto Add = [&L](EGroundSpecies Id, const TCHAR* Name, float HMin, float HMax, float Wind, float Draw, EFloraUse Uses, FLinearColor Dye) -> FGroundSpecies&
    {
        FGroundSpecies& G = L.AddDefaulted_GetRef();
        G.Id = Id; G.Name = Name; G.HeightMin = HMin; G.HeightMax = HMax; G.Wind = Wind; G.DrawDistance = Draw; G.Uses = Uses; G.DyeColor = Dye;
        return G;
    };
    using U = EFloraUse;
    Add(EGroundSpecies::Grass, TEXT("Capim"), .35f, .55f, 1.f, 75.f, U::Fiber, FLinearColor::Transparent).bFoliageMaterial = true;
    Add(EGroundSpecies::Fern, TEXT("Samambaia"), .35f, .6f, .7f, 110.f, U::Harvest | U::Alchemy, FLinearColor::Transparent);
    Add(EGroundSpecies::Sunflower, TEXT("Girassol"), 1.4f, 2.2f, .35f, 260.f, U::Harvest | U::Seeds | U::Food | U::Dye | U::Pollinator | U::Ornamental, FLinearColor(.85f, .55f, .02f));
    Add(EGroundSpecies::WildRose, TEXT("Rosa-Silvestre"), .6f, 1.f, .4f, 180.f, U::Harvest | U::Perfume | U::Dye | U::Pollinator | U::Ornamental, FLinearColor(.6f, .03f, .08f));
    Add(EGroundSpecies::Lavender, TEXT("Lavanda"), .45f, .7f, .8f, 180.f, U::Harvest | U::Perfume | U::Alchemy | U::Pollinator | U::Dye, FLinearColor(.25f, .15f, .55f));
    Add(EGroundSpecies::Daisy, TEXT("Margarida"), .25f, .4f, .9f, 110.f, U::Harvest | U::Pollinator | U::Ornamental, FLinearColor::Transparent);
    Add(EGroundSpecies::Poppy, TEXT("Papoula"), .35f, .55f, .9f, 120.f, U::Harvest | U::Alchemy | U::Dye | U::Pollinator, FLinearColor(.7f, .05f, .02f));
    Add(EGroundSpecies::Tulip, TEXT("Tulipa"), .3f, .45f, .8f, 120.f, U::Harvest | U::Ornamental | U::Pollinator, FLinearColor::Transparent);
    Add(EGroundSpecies::AlpineFlower, TEXT("Flor-Alpina"), .1f, .18f, .5f, 90.f, U::Harvest | U::Alchemy | U::Pollinator, FLinearColor(.06f, .12f, .6f));
    return L;
}

// A box with its own axes (half extents along each), for leaning trunks, branches, fronds and petals.
void FloraOrientedBox(FAhgMeshBuilder& Out, const FVector3f& C, const FVector3f& X, const FVector3f& Y, const FVector3f& Z, const FVector3f& Half)
{
    auto P = [&](float SX, float SY, float SZ) { return C + X * (SX * Half.X) + Y * (SY * Half.Y) + Z * (SZ * Half.Z); };
    const FVector3f V[8] = { P(-1, -1, -1), P(1, -1, -1), P(1, 1, -1), P(-1, 1, -1), P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1) };
    Out.Quad(V[4], V[5], V[6], V[7]);
    Out.Quad(V[3], V[2], V[1], V[0]);
    Out.Quad(V[0], V[1], V[5], V[4]);
    Out.Quad(V[2], V[3], V[7], V[6]);
    Out.Quad(V[1], V[2], V[6], V[5]);
    Out.Quad(V[3], V[0], V[4], V[7]);
}

// A box segment from A to B (an angular branch or a stem), Thick x Thick in section.
void FloraBeam(FAhgMeshBuilder& Out, const FVector3f& A, const FVector3f& B, float Thick)
{
    const FVector3f Axis = (B - A).GetSafeNormal();
    const FVector3f Side = FVector3f::CrossProduct(FMath::Abs(Axis.Z) > .95f ? FVector3f(1, 0, 0) : FVector3f(0, 0, 1), Axis).GetSafeNormal();
    const FVector3f Up = FVector3f::CrossProduct(Axis, Side);
    FloraOrientedBox(Out, (A + B) * .5f, Axis, Side, Up, FVector3f((B - A).Size() * .5f, Thick * .5f, Thick * .5f));
}

void FloraBox(FAhgMeshBuilder& Out, const FVector3f& Center, const FVector3f& Size)
{
    Out.Box(Center - FVector3f(Size.X, Size.Y, 0) * .5f, Center + FVector3f(Size.X * .5f, Size.Y * .5f, Size.Z));
}

// A cluster of leaf cubes around Center (blocky canopies with small natural irregularities).
void FloraLeafCluster(FAhgMeshBuilder& Out, const FVector3f& Center, float Size, int32 Cubes, int32 Seed, int32 Salt)
{
    FloraBox(Out, Center - FVector3f(0, 0, Size * .45f), FVector3f(Size, Size * (.85f + .3f * FloraHash01(Seed, Salt, 0, 91)), Size * .8f));
    for (int32 K = 0; K < Cubes; ++K)
    {
        const float A = 2.f * PI * (K + FloraHash01(Seed, Salt, K, 92) * .6f) / FMath::Max(Cubes, 1);
        const float S = Size * (.4f + .25f * FloraHash01(Seed, Salt, K, 93));
        const FVector3f O(FMath::Cos(A) * Size * .5f, FMath::Sin(A) * Size * .5f, (FloraHash01(Seed, Salt, K, 94) - .3f) * Size * .5f);
        FloraBox(Out, Center + O - FVector3f(0, 0, S * .5f), FVector3f(S, S, S));
    }
}

void FloraHeightFraction(FAhgMeshBuilder& Out, int32 First, float Top)
{
    Top = FMath::Max(Top, 1.f);
    for (int32 I = First; I < Out.Verts.Num(); ++I)
    {
        Out.Verts[I].UV = FVector2f(0.f, FMath::Clamp(Out.Verts[I].P.Z / Top, 0.f, 1.f));
    }
}
}

// ---------------------------------------------------------------- environment

FFloraEnvironment FFloraEnvironment::From(const FWorldSample& S, const FVector3f& Normal)
{
    FFloraEnvironment E;
    E.Biome = S.Biome;
    E.Rock = S.Rock;
    E.Temperature = S.Temperature;
    E.Moisture = S.Moisture;
    E.Altitude = S.Height;
    E.Slope = FMath::Clamp(1.f - Normal.Z, 0.f, 1.f);
    E.ForestDensity = S.ForestDensity;
    E.bWater = S.IsWater();
    // Sun: slopes facing the midday sun (from -Y, high in the sky) get more light than slopes facing away.
    const FVector3f SunDir = FVector3f(.25f, -.55f, .8f).GetSafeNormal();
    E.Sun = FMath::Clamp(FVector3f::DotProduct(Normal.GetSafeNormal(), SunDir) * 1.15f - .05f, 0.f, 1.f);
    E.Water = FMath::Clamp(FMath::Max(S.Moisture, 1.f - S.WaterDistance / 40.f), 0.f, 1.f);
    float Rock = .5f;
    switch (S.Rock)
    {
    case EAmeRock::Limestone: case EAmeRock::Basalt: Rock = .75f; break;
    case EAmeRock::Sandstone: Rock = .55f; break;
    case EAmeRock::Granite: case EAmeRock::Slate: Rock = .42f; break;
    default: break;
    }
    const float Wet = FloraWindow(S.Moisture, .4f, .78f, .3f);
    E.Fertility = FMath::Clamp(Rock * (.45f + .55f * Wet) * (1.f - E.Slope * 1.4f) + (S.WaterDistance < 30.f ? .15f : 0.f), 0.f, 1.f);
    return E;
}

// ---------------------------------------------------------------- trees

const TArray<FFloraSpecies>& FFloraRegistry::All()
{
    static const TArray<FFloraSpecies> Species = FloraMakeSpecies();
    return Species;
}

const FFloraSpecies& FFloraRegistry::Get(EFloraSpecies Id)
{
    return All()[static_cast<int32>(Id)];
}

TArray<FString> FFloraRegistry::Validate()
{
    TArray<FString> Problems;
    const TArray<FFloraSpecies>& L = All();
    if (L.Num() != static_cast<int32>(EFloraSpecies::Count))
    {
        Problems.Add(FString::Printf(TEXT("%d species for %d ids"), L.Num(), static_cast<int32>(EFloraSpecies::Count)));
    }
    TSet<FString> Names;
    for (int32 I = 0; I < L.Num(); ++I)
    {
        const FFloraSpecies& S = L[I];
        if (static_cast<int32>(S.Id) != I)
        {
            Problems.Add(FString::Printf(TEXT("%s out of order"), S.Name));
        }
        bool bDuplicate = false;
        Names.Add(S.Name, &bDuplicate);
        if (bDuplicate || !*S.Name)
        {
            Problems.Add(FString::Printf(TEXT("name '%s' empty or repeated"), S.Name));
        }
        if (S.ScaleMin <= 0.f || S.ScaleMax < S.ScaleMin || S.Wind < 0.f || S.Wind > 1.f || S.CrownWidth <= 0.f || S.CrownHeight <= 0.f
            || S.TrunkHeight < 0.f || S.CrownTiers < 1 || S.DrawDistance <= 0.f || S.MinTemperature >= S.MaxTemperature || S.MinMoisture >= S.MaxMoisture)
        {
            Problems.Add(FString::Printf(TEXT("%s: parameter out of range"), S.Name));
        }
        float Total = 0.f;
        for (float W : S.BiomeWeight)
        {
            Total += W;
        }
        if (Total <= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("%s grows nowhere"), S.Name));
        }
        if (FAmeRegistry::Get(S.Bark).Category != EAmeCategory::Vegetation || FAmeRegistry::Get(S.Leaves).Category != EAmeCategory::Vegetation)
        {
            Problems.Add(FString::Printf(TEXT("%s: bark and leaves must be vegetation surfaces"), S.Name));
        }
    }
    const TArray<FGroundSpecies>& G = FFloraGen::Ground();
    for (int32 I = 0; I < G.Num(); ++I)
    {
        if (static_cast<int32>(G[I].Id) != I || G[I].HeightMin <= 0.f || G[I].HeightMax < G[I].HeightMin || G[I].DrawDistance <= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("ground species %s invalid"), G[I].Name));
        }
        if (EnumHasAnyFlags(G[I].Uses, EFloraUse::Dye) && G[I].DyeColor.A <= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("%s gives dye but has no dye colour"), G[I].Name));
        }
    }
    if (G.Num() != static_cast<int32>(EGroundSpecies::Count))
    {
        Problems.Add(TEXT("ground species count"));
    }
    return Problems;
}

int32 FFloraRegistry::Pick(EAmeBiome Biome, float Temperature, float Roll)
{
    FFloraEnvironment Env;
    Env.Biome = Biome;
    Env.Temperature = Temperature;
    Env.Moisture = .5f;
    return PickAt(Env, 0, FVector2D(1e7, 1e7), Roll); // far away: no regional groves
}

int32 FFloraRegistry::PickAt(const FFloraEnvironment& Env, int32 Seed, FVector2D P, float Roll)
{
    const int32 Index = static_cast<int32>(Env.Biome);
    const bool bRegional = P.X < 1e6;
    float Weights[static_cast<int32>(EFloraSpecies::Count)] = {};
    float Total = 0.f;
    for (const FFloraSpecies& S : All())
    {
        float W = S.BiomeWeight[Index];
        if (W <= 0.f || Env.Temperature < S.MinTemperature || Env.Temperature > S.MaxTemperature)
        {
            continue;
        }
        W *= FloraWindow(Env.Moisture, S.MinMoisture, S.MaxMoisture, .12f);
        // Groves: each species is common in some regions and scarce in others (not the same mix everywhere).
        if (bRegional && S.IsCommon())
        {
            W *= .3f + 1.4f * FFloraGen::Noise(Seed, P, 420.0, 600u + static_cast<uint32>(S.Id));
        }
        Weights[static_cast<int32>(S.Id)] = W;
        Total += W;
    }
    if (Total <= 0.f)
    {
        return -1;
    }
    float Left = Roll * Total;
    int32 Last = -1;
    for (int32 I = 0; I < static_cast<int32>(EFloraSpecies::Count); ++I)
    {
        if (Weights[I] > 0.f)
        {
            Last = I;
            Left -= Weights[I];
            if (Left < 0.f)
            {
                return I;
            }
        }
    }
    return Last;
}

void FFloraRegistry::BuildMesh(const FFloraSpecies& S, int32 Seed, int32 Variant, FAhgMeshBuilder& Out)
{
    const float TH = S.TrunkHeight * 100.f, TW = S.TrunkWidth * 100.f, CW = S.CrownWidth * 100.f, CH = S.CrownHeight * 100.f;
    const int32 Salt = static_cast<int32>(S.Id) * 101 + Variant * 13;
    auto R = [Seed, Salt](int32 K) { return FloraHash01(Seed, Salt, K, 77u); }; // fixed per species, variant and seed
    // Variants change proportions too: taller and slimmer, or lower and wider.
    const float Stretch = Variant == 1 ? 1.15f : Variant == 2 ? .88f : 1.f;
    const float Spread = Variant == 1 ? .88f : Variant == 2 ? 1.14f : 1.f;
    const int32 First = Out.Verts.Num();
    switch (S.Form)
    {
    case EFloraForm::Blocky:
    {
        const float H = TH * Stretch, W = CW * Spread;
        if (TH > 0.f)
        {
            // Segmented trunk: stacked blocks that narrow and shift a little.
            Out.Surface = FloraSurface(S.Bark);
            const int32 Segments = 3;
            FVector3f At(0, 0, -30);
            for (int32 K = 0; K < Segments; ++K)
            {
                const float SegH = (H + 30.f + CH * .25f) / Segments;
                const float SW = TW * (1.f - .18f * K);
                FloraBox(Out, At, FVector3f(SW, SW, SegH + 2.f));
                At += FVector3f((R(K) - .5f) * TW * .25f, (R(K + 5) - .5f) * TW * .25f, SegH);
            }
        }
        Out.Surface = FloraSurface(S.Leaves);
        if (S.Id == EFloraSpecies::Willow)
        {
            // A wide flat canopy and hanging rectangular curtains along its edges.
            FloraBox(Out, FVector3f(0, 0, H), FVector3f(W, W, CH * .45f));
            FloraBox(Out, FVector3f(0, 0, H + CH * .45f), FVector3f(W * .6f, W * .6f, CH * .3f));
            for (int32 K = 0; K < 12; ++K)
            {
                const float A = 2.f * PI * K / 12.f;
                const float Drop = CH * (.7f + .5f * R(K));
                FloraBox(Out, FVector3f(FMath::Cos(A) * W * .44f, FMath::Sin(A) * W * .44f, H + CH * .3f - Drop), FVector3f(W * .14f, W * .14f, Drop));
            }
            break;
        }
        if (TH <= 0.f)
        {
            // Bush: a low cluster of leaf cubes.
            FloraLeafCluster(Out, FVector3f(0, 0, CH * .55f), W * .6f, 4 + Variant, Seed, Salt);
            break;
        }
        // Angular branches (boxes at 35-55 degrees) carrying leaf clusters, and a crown cluster on top.
        const int32 Branches = 3 + Variant % 2;
        for (int32 K = 0; K < Branches; ++K)
        {
            const float A = 2.f * PI * (K + R(20 + K) * .5f) / Branches;
            const float Lift = FMath::DegreesToRadians(35.f + 20.f * R(30 + K));
            const float Len = W * (.35f + .15f * R(40 + K));
            const FVector3f Base(0, 0, H * (.55f + .3f * R(50 + K)));
            const FVector3f Tip = Base + FVector3f(FMath::Cos(A) * FMath::Cos(Lift), FMath::Sin(A) * FMath::Cos(Lift), FMath::Sin(Lift)) * Len;
            Out.Surface = FloraSurface(S.Bark);
            FloraBeam(Out, Base, Tip, TW * .45f);
            Out.Surface = FloraSurface(S.Leaves);
            FloraLeafCluster(Out, Tip + FVector3f(0, 0, CH * .15f), W * (.34f + .12f * R(60 + K)), 2, Seed, Salt + K * 7);
        }
        float Z = H + CH * .1f;
        for (int32 T = 0; T < S.CrownTiers; ++T)
        {
            const float F = 1.f - .24f * T;
            const float Hgt = CH * (T == 0 ? .4f : .6f / FMath::Max(S.CrownTiers - 1, 1));
            const FVector3f Off((R(T * 3) - .5f) * W * .14f, (R(T * 3 + 1) - .5f) * W * .14f, 0.f);
            FloraBox(Out, FVector3f(0, 0, Z) + Off, FVector3f(W * .62f * F, W * .62f * F * (.85f + .25f * R(T * 3 + 2)), Hgt));
            Z += Hgt * .85f;
        }
        break;
    }
    case EFloraForm::Conical:
    {
        Out.Surface = FloraSurface(S.Bark);
        FloraBox(Out, FVector3f(0, 0, -30), FVector3f(TW, TW, TH * Stretch + CH * .6f));
        Out.Surface = FloraSurface(S.Leaves);
        // Square pyramids (4-sided cones), overlapping, narrowing to a point: a stepped triangle. The
        // variants change the tier count and each tier turns a little (a less regular silhouette).
        const int32 Tiers = S.CrownTiers + (Variant == 1 ? 1 : Variant == 2 ? -1 : 0);
        const float Crown = CH * Stretch;
        const float TierH = Crown / (Tiers * .72f + .28f);
        for (int32 T = 0; T < Tiers; ++T)
        {
            const float F = 1.f - static_cast<float>(T) / (Tiers + .4f);
            const int32 First2 = Out.Verts.Num();
            Out.Cylinder(FVector3f(0, 0, TH * Stretch + T * TierH * .72f), CW * Spread * .5f * F * 1.414f, TierH, 4, true, 0.f);
            const float Turn = (R(70 + T) - .5f) * .5f;
            const float C = FMath::Cos(Turn), Sn = FMath::Sin(Turn);
            for (int32 I = First2; I < Out.Verts.Num(); ++I)
            {
                FAhgMeshBuilder::FVert& V = Out.Verts[I];
                V.P = FVector3f(V.P.X * C - V.P.Y * Sn, V.P.X * Sn + V.P.Y * C, V.P.Z);
                V.N = FVector3f(V.N.X * C - V.N.Y * Sn, V.N.X * Sn + V.N.Y * C, V.N.Z);
            }
        }
        break;
    }
    case EFloraForm::Angular:
    {
        if (S.Id == EFloraSpecies::DeadTree)
        {
            // Dry tree: a bare segmented trunk with forked angular branches, no leaves.
            Out.Surface = FloraSurface(S.Bark);
            FloraBox(Out, FVector3f(0, 0, -30), FVector3f(TW, TW, TH * Stretch + 30.f));
            for (int32 K = 0; K < S.CrownTiers; ++K)
            {
                const float A = 2.f * PI * (K + R(K) * .7f) / S.CrownTiers;
                const FVector3f Base(0, 0, TH * Stretch * (.45f + .5f * R(10 + K)));
                const FVector3f Mid = Base + FVector3f(FMath::Cos(A) * CW * .3f, FMath::Sin(A) * CW * .3f, CH * (.4f + .3f * R(20 + K)));
                FloraBeam(Out, Base, Mid, TW * .4f);
                const FVector3f Tip = Mid + FVector3f(FMath::Cos(A + .6f) * CW * .18f, FMath::Sin(A + .6f) * CW * .18f, CH * .35f);
                FloraBeam(Out, Mid, Tip, TW * .25f);
            }
            break;
        }
        // Palm: a segmented trunk leaning in steps, and flat rectangular fronds bent down from the crown.
        Out.Surface = FloraSurface(S.Bark);
        const int32 Segments = 6;
        FVector3f At(0, 0, -30);
        const float Lean = .1f + .1f * Variant;
        for (int32 K = 0; K < Segments; ++K)
        {
            const float SegH = (TH * Stretch + 30.f) / Segments;
            const float W = TW * (1.15f - .3f * K / Segments);
            FloraBox(Out, At, FVector3f(W, W, SegH + 4.f));
            At += FVector3f(SegH * Lean * (1.f + .5f * K / Segments), 0, SegH);
        }
        Out.Surface = FloraSurface(S.Leaves);
        FloraBox(Out, At - FVector3f(0, 0, 20.f), FVector3f(TW * 1.6f, TW * 1.6f, 50.f));
        for (int32 K = 0; K < S.CrownTiers; ++K)
        {
            const float A = 2.f * PI * (K + .3f * R(K)) / S.CrownTiers;
            const FVector3f Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
            const float Len = CW * Spread * .5f * (.8f + .3f * R(10 + K));
            const float Bend = -.35f - .25f * R(20 + K);
            const FVector3f Axis = (Dir + FVector3f(0, 0, Bend)).GetSafeNormal();
            const FVector3f Width = FVector3f::CrossProduct(FVector3f(0, 0, 1), Dir).GetSafeNormal();
            const FVector3f Up = FVector3f::CrossProduct(Axis, Width).GetSafeNormal();
            FloraOrientedBox(Out, At + Axis * (Len * .5f) + FVector3f(0, 0, 10.f), Axis, Width, Up, FVector3f(Len * .5f, CW * .07f, 6.f));
        }
        break;
    }
    case EFloraForm::Organic:
    {
        // Rare and old: a flaring trunk, root swellings and a heavy irregular rounded crown.
        Out.Surface = FloraSurface(S.Bark);
        Out.Cylinder(FVector3f(0, 0, -40), TW * .62f, TH + 120.f, 10, false, TW * .3f);
        for (int32 K = 0; K < 5; ++K)
        {
            const float A = 2.f * PI * (K + R(K)) / 5.f;
            Out.Sphere(FVector3f(FMath::Cos(A) * TW * .55f, FMath::Sin(A) * TW * .55f, 10.f), FVector3f(TW * .4f, TW * .4f, TW * .35f), 4, 7, .25f, Seed + K);
        }
        for (int32 K = 0; K < 3; ++K)
        {
            const float A = 2.f * PI * (K + R(50 + K)) / 3.f;
            Out.Cylinder(FVector3f(FMath::Cos(A) * TW * .2f, FMath::Sin(A) * TW * .2f, TH * .75f), TW * .22f, CH * .45f, 7, false, TW * .08f);
        }
        Out.Surface = FloraSurface(S.Leaves);
        for (int32 K = 0; K < S.CrownTiers; ++K)
        {
            const float A = 2.f * PI * (K + R(60 + K)) / S.CrownTiers;
            const float Rad = CW * (.18f + .1f * R(70 + K));
            const float D = K == 0 ? 0.f : CW * (.22f + .14f * R(80 + K));
            Out.Sphere(FVector3f(FMath::Cos(A) * D, FMath::Sin(A) * D, TH + CH * (.35f + .35f * R(90 + K))), FVector3f(Rad, Rad, Rad * .8f), 6, 9, .25f, Seed + 100 + K);
        }
        // A few glowing moss patches on the roots: the special tree is a little magical.
        Out.Surface = FloraSurface(EAmeSurface::GlowMoss);
        for (int32 K = 0; K < 3; ++K)
        {
            const float A = 2.f * PI * (K + R(110 + K)) / 3.f;
            FloraBox(Out, FVector3f(FMath::Cos(A) * TW * .6f, FMath::Sin(A) * TW * .6f, 0.f), FVector3f(TW * .3f, TW * .3f, 18.f));
        }
        break;
    }
    }
    FloraHeightFraction(Out, First, MeshHeight(S) * FMath::Max(Stretch, 1.f));
}

// ---------------------------------------------------------------- ground flora and fields

const TArray<FGroundSpecies>& FFloraGen::Ground()
{
    static const TArray<FGroundSpecies> Species = FloraMakeGround();
    return Species;
}

const FGroundSpecies& FFloraGen::Get(EGroundSpecies Id)
{
    return Ground()[static_cast<int32>(Id)];
}

float FFloraGen::Noise(int32 Seed, FVector2D P, double Scale, uint32 Salt)
{
    const FVector2D Q = P / Scale + FVector2D(Salt * 17.31, Salt * 9.17);
    const int32 X = FMath::FloorToInt(Q.X), Y = FMath::FloorToInt(Q.Y);
    float FX = static_cast<float>(Q.X - X), FY = static_cast<float>(Q.Y - Y);
    FX = FX * FX * (3.f - 2.f * FX);
    FY = FY * FY * (3.f - 2.f * FY);
    auto H = [Seed, Salt](int32 I, int32 J) { return FloraHash01(Seed, I, J, 1000u + Salt); };
    const float A = FMath::Lerp(H(X, Y), H(X + 1, Y), FX), B = FMath::Lerp(H(X, Y + 1), H(X + 1, Y + 1), FX);
    return FMath::Lerp(A, B, FY);
}

FFieldSample FFloraGen::FieldAt(int32 Seed, FVector2D P, const FFloraEnvironment& Env)
{
    FFieldSample Best;
    if (Env.bWater)
    {
        return Best;
    }
    auto Biome = [&Env](std::initializer_list<EAmeBiome> List) { for (EAmeBiome B : List) { if (Env.Biome == B) return true; } return false; };
    auto Consider = [&](EFlowerField Field, float Suit, double Scale, float Threshold)
    {
        if (Suit <= 0.f)
        {
            return;
        }
        // Two octaves: big rounded regions with irregular edges.
        const float N = .72f * Noise(Seed, P, Scale, 40u + static_cast<uint32>(Field)) + .28f * Noise(Seed, P, Scale * .3, 50u + static_cast<uint32>(Field));
        const float Strength = FMath::Clamp((N - Threshold) / .1f, 0.f, 1.f) * Suit;
        if (Strength > Best.Strength)
        {
            Best.Field = Field;
            Best.Strength = Strength;
        }
    };
    const bool bFlat = Env.Slope < .14f;
    // Sunflowers: sunny, fertile, flat, warm plains.
    if (Biome({ FB::Plains, FB::Meadow }) && bFlat)
    {
        Consider(EFlowerField::Sunflower, FloraWindow(Env.Temperature, 12.f, 32.f, 4.f) * FloraWindow(Env.Moisture, .3f, .72f, .1f)
            * FMath::Clamp((Env.Sun - .45f) / .2f, 0.f, 1.f) * FMath::Clamp((Env.Fertility - .25f) / .2f, 0.f, 1.f), 280.0, .6f);
    }
    // Lavender: sunny, rather dry, poorer or chalky ground, also on gentle hills.
    if (Biome({ FB::Plains, FB::Meadow, FB::Mountain }) && Env.Slope < .3f && Env.Altitude < 240.f)
    {
        Consider(EFlowerField::Lavender, FloraWindow(Env.Temperature, 8.f, 30.f, 4.f) * FloraWindow(Env.Moisture, .12f, .55f, .1f)
            * FMath::Clamp((Env.Sun - .5f) / .2f, 0.f, 1.f) * (Env.Rock == EAmeRock::Limestone || Env.Rock == EAmeRock::Sandstone ? 1.f : .75f), 230.0, .63f);
    }
    // Wild roses: meadows, forest edges and river banks, moist and mild.
    if (Biome({ FB::Meadow, FB::Plains, FB::RiverBank }) || (Env.Biome == FB::Forest && Env.ForestDensity < .4f))
    {
        Consider(EFlowerField::WildRose, FloraWindow(Env.Temperature, 6.f, 28.f, 4.f) * FloraWindow(Env.Moisture, .42f, .85f, .1f), 170.0, .65f);
    }
    // Mixed wildflowers: the commonest field, mostly in meadows.
    if (Biome({ FB::Meadow, FB::Plains }) && Env.Slope < .25f)
    {
        Consider(EFlowerField::Wildflower, FloraWindow(Env.Temperature, 5.f, 30.f, 4.f) * (Env.Biome == FB::Meadow ? 1.f : .7f), 190.0, .61f);
    }
    // Alpine flowers: cold mountain pastures.
    if (Biome({ FB::Mountain, FB::SnowPeak }) && Env.Slope < .45f)
    {
        Consider(EFlowerField::Alpine, FloraWindow(Env.Temperature, -2.f, 12.f, 3.f), 130.0, .6f);
    }
    return Best;
}

FLinearColor FFloraGen::FieldColor(EFlowerField Field)
{
    switch (Field)
    {
    case EFlowerField::Sunflower: return FLinearColor(.62f, .42f, .03f);
    case EFlowerField::WildRose: return FLinearColor(.36f, .1f, .12f);
    case EFlowerField::Lavender: return FLinearColor(.2f, .14f, .38f);
    case EFlowerField::Wildflower: return FLinearColor(.45f, .4f, .3f);
    case EFlowerField::Alpine: return FLinearColor(.14f, .2f, .45f);
    default: return FLinearColor::Black;
    }
}

const TCHAR* FFloraGen::FieldName(EFlowerField Field)
{
    static const TCHAR* Names[] = { TEXT("nenhum"), TEXT("girassóis"), TEXT("rosas silvestres"), TEXT("lavanda"), TEXT("flores silvestres"), TEXT("flores alpinas") };
    return Names[FMath::Clamp(static_cast<int32>(Field), 0, 5)];
}

FVector2D FFloraGen::GroundPosition(int32 Seed, FIntPoint C)
{
    return FVector2D((C.X + FloraHash01(Seed, C.X, C.Y, 11)) * GroundCell, (C.Y + FloraHash01(Seed, C.X, C.Y, 12)) * GroundCell);
}

bool FFloraGen::GroundAt(int32 Seed, FIntPoint C, const FFloraEnvironment& Env, FGroundPlant& Out)
{
    if (Env.bWater || Env.Slope > .5f)
    {
        return false;
    }
    const FVector2D P = GroundPosition(Seed, C);
    const float Roll = FloraHash01(Seed, C.X, C.Y, 13), Pick = FloraHash01(Seed, C.X, C.Y, 16);
    EGroundSpecies Species = EGroundSpecies::Count;
    const FFieldSample Field = FieldAt(Seed, P, Env);
    if (Field.Strength > 0.f && Roll < .25f + .65f * Field.Strength)
    {
        switch (Field.Field)
        {
        case EFlowerField::Sunflower: Species = Pick < .9f ? EGroundSpecies::Sunflower : EGroundSpecies::Grass; break;
        case EFlowerField::Lavender: Species = Pick < .85f ? EGroundSpecies::Lavender : EGroundSpecies::Grass; break;
        case EFlowerField::WildRose: Species = Pick < .45f ? EGroundSpecies::WildRose : Pick < .7f ? EGroundSpecies::Daisy : EGroundSpecies::Grass; break;
        case EFlowerField::Alpine: Species = Pick < .75f ? EGroundSpecies::AlpineFlower : EGroundSpecies::Grass; break;
        default:
        {
            // Mixed field: patches of one colour inside it (a sub-noise), not salt-and-pepper.
            const float Patch = Noise(Seed, P, 18.0, 77u);
            Species = Pick < .25f ? EGroundSpecies::Grass : Patch < .35f ? EGroundSpecies::Daisy : Patch < .62f ? EGroundSpecies::Poppy : EGroundSpecies::Tulip;
            break;
        }
        }
    }
    else
    {
        const bool bGrassy = Env.Biome == FB::Plains || Env.Biome == FB::Meadow || Env.Biome == FB::RiverBank || Env.Biome == FB::Wetland;
        if (Env.Biome == FB::Forest)
        {
            // Forest floor: ferns in the shade, grass in clearings.
            Species = Roll < .25f + .2f * Env.ForestDensity ? EGroundSpecies::Fern : Roll < .5f ? EGroundSpecies::Grass : EGroundSpecies::Count;
        }
        else if (bGrassy)
        {
            Species = Roll < .03f + (Env.Biome == FB::Meadow ? .05f : 0.f) ? (Pick < .6f ? EGroundSpecies::Daisy : EGroundSpecies::Poppy)
                : Roll < .8f ? EGroundSpecies::Grass : EGroundSpecies::Count;
        }
        else if (Env.Biome == FB::Mountain && Env.Altitude < 300.f)
        {
            Species = Roll < .3f ? EGroundSpecies::Grass : Roll < .34f ? EGroundSpecies::AlpineFlower : EGroundSpecies::Count;
        }
    }
    if (Species == EGroundSpecies::Count)
    {
        return false;
    }
    const FGroundSpecies& Def = Get(Species);
    Out.Species = Species;
    Out.Position = P;
    Out.Scale = FMath::Lerp(Def.HeightMin, Def.HeightMax, FloraHash01(Seed, C.X, C.Y, 15)) / Def.HeightMax;
    Out.Yaw = 360.f * FloraHash01(Seed, C.X, C.Y, 14);
    Out.Tint = FloraHash01(Seed, C.X, C.Y, 17) - .5f;
    return true;
}

void FFloraGen::BuildMesh(const FGroundSpecies& G, int32 Seed, FAhgMeshBuilder& B)
{
    const int32 First = B.Verts.Num();
    const float H = G.HeightMax * 100.f;
    auto Leaf = FloraSurface(FS::Fern);
    switch (G.Id)
    {
    case EGroundSpecies::Grass:
    {
        // Thin bent blades; vertex colour R = height along the blade (wind weight in the foliage material).
        B.Surface = FloraSurface(FS::Grass);
        for (int32 I = 0; I < 9; ++I)
        {
            const float A = I * 2.4f, R = 8.f + 9.f * (I % 3);
            const FVector3f Root(R * FMath::Cos(A), R * FMath::Sin(A), -2.f);
            const FVector3f Side(-FMath::Sin(A) * 3.5f, FMath::Cos(A) * 3.5f, 0.f);
            const float BH = 38.f + 7.f * (I % 4);
            const FVector3f Tip = Root + FVector3f(FMath::Cos(A) * 14.f, FMath::Sin(A) * 14.f, BH);
            B.Color = FLinearColor(0, 0, 0, 1);
            B.Tri(Root - Side, Root + Side, Tip);
            B.Verts.Last().Color = FLinearColor(1, 0, 0, 1);
        }
        B.Color = FLinearColor::White;
        break;
    }
    case EGroundSpecies::Fern:
    {
        B.Surface = Leaf;
        for (int32 K = 0; K < 7; ++K)
        {
            const float A = 2.f * PI * (K + FloraHash01(Seed, K, 1, 21) * .4f) / 7.f;
            const FVector3f Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
            const FVector3f Mid = Dir * 22.f + FVector3f(0, 0, H * .7f);
            FloraBeam(B, FVector3f(0, 0, 0), Mid, 5.f);
            FloraBeam(B, Mid, Mid + Dir * 26.f + FVector3f(0, 0, -H * .15f), 7.f); // the frond tip bends down
        }
        break;
    }
    case EGroundSpecies::Sunflower:
    {
        B.Surface = Leaf;
        FloraBox(B, FVector3f(0, 0, -2), FVector3f(6.f, 6.f, H * .86f));
        for (int32 K = 0; K < 3; ++K)
        {
            const float A = K * 2.2f;
            const FVector3f Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
            const FVector3f At(0, 0, H * (.3f + .17f * K));
            FloraBeam(B, At, At + Dir * 28.f + FVector3f(0, 0, 8.f), 14.f - K * 2.f);
        }
        // The head faces the morning sun (+X), tilted a little down: a square disc and a ring of petals.
        const FVector3f C(8.f, 0.f, H * .88f);
        const FVector3f Nh = FVector3f(.92f, 0.f, -.2f).GetSafeNormal();
        const FVector3f U(0, 1, 0);
        const FVector3f V = FVector3f::CrossProduct(Nh, U);
        B.Surface = FloraSurface(FS::SunflowerDisk);
        FloraOrientedBox(B, C, U, V, Nh, FVector3f(15.f, 15.f, 4.f));
        B.Surface = FloraSurface(FS::PetalSunflower);
        for (int32 K = 0; K < 12; ++K)
        {
            const float A = 2.f * PI * K / 12.f;
            const FVector3f D = U * FMath::Cos(A) + V * FMath::Sin(A);
            const FVector3f Perp = U * -FMath::Sin(A) + V * FMath::Cos(A);
            FloraOrientedBox(B, C + D * 22.f - Nh * 1.f, D, Perp, Nh, FVector3f(9.f, 4.f, 1.2f));
        }
        break;
    }
    case EGroundSpecies::WildRose:
    {
        B.Surface = FloraSurface(FS::LeavesBroad);
        FloraLeafCluster(B, FVector3f(0, 0, H * .5f), H * .75f, 4, Seed, 300);
        // Roses: small diamond blocks (a box turned 45 degrees) on the bush.
        B.Surface = FloraSurface(FS::PetalRose);
        for (int32 K = 0; K < 8; ++K)
        {
            const float A = 2.f * PI * (K + FloraHash01(Seed, K, 2, 22) * .5f) / 8.f;
            const FVector3f P(FMath::Cos(A) * H * .4f, FMath::Sin(A) * H * .4f, H * (.45f + .45f * FloraHash01(Seed, K, 3, 23)));
            const FVector3f X = FVector3f(FMath::Cos(A + PI / 4), FMath::Sin(A + PI / 4), 0.f);
            const FVector3f Y = FVector3f(-X.Y, X.X, 0.f);
            FloraOrientedBox(B, P, X, Y, FVector3f(0, 0, 1), FVector3f(6.f, 6.f, 5.f));
        }
        break;
    }
    case EGroundSpecies::Lavender:
    {
        B.Surface = Leaf;
        FloraBox(B, FVector3f(0, 0, -2), FVector3f(34.f, 34.f, H * .25f));
        for (int32 K = 0; K < 11; ++K)
        {
            const float A = 2.f * PI * (K + FloraHash01(Seed, K, 4, 24) * .5f) / 11.f;
            const float R = 6.f + 10.f * FloraHash01(Seed, K, 5, 25);
            const float Top = H * (.8f + .2f * FloraHash01(Seed, K, 6, 26));
            const FVector3f Base(FMath::Cos(A) * R * .4f, FMath::Sin(A) * R * .4f, H * .2f);
            const FVector3f Spike(FMath::Cos(A) * R, FMath::Sin(A) * R, Top * .7f);
            B.Surface = Leaf;
            FloraBeam(B, Base, Spike, 2.f);
            B.Surface = FloraSurface(FS::PetalLavender);
            FloraBeam(B, Spike, Spike + FVector3f(FMath::Cos(A) * 2.f, FMath::Sin(A) * 2.f, Top * .3f), 4.5f);
        }
        break;
    }
    case EGroundSpecies::Daisy:
    case EGroundSpecies::Poppy:
    case EGroundSpecies::Tulip:
    {
        B.Surface = Leaf;
        FloraBox(B, FVector3f(0, 0, -2), FVector3f(1.6f, 1.6f, H * .85f));
        FloraBeam(B, FVector3f(0, 0, H * .2f), FVector3f(9.f, 2.f, H * .35f), 4.f);
        const FVector3f C(0, 0, H * .86f);
        if (G.Id == EGroundSpecies::Daisy)
        {
            B.Surface = FloraSurface(FS::PetalDaisy);
            for (int32 K = 0; K < 8; ++K)
            {
                const float A = 2.f * PI * K / 8.f;
                const FVector3f D(FMath::Cos(A), FMath::Sin(A), 0.f);
                FloraOrientedBox(B, C + D * 4.f, D, FVector3f(-D.Y, D.X, 0.f), FVector3f(0, 0, 1), FVector3f(3.f, 1.2f, .6f));
            }
            B.Surface = FloraSurface(FS::PetalSunflower);
            FloraBox(B, C - FVector3f(0, 0, .5f), FVector3f(2.4f, 2.4f, 1.6f));
        }
        else
        {
            // A cup of four petals opening outward (poppy) or nearly closed (tulip).
            B.Surface = FloraSurface(G.Id == EGroundSpecies::Poppy ? FS::PetalPoppy : FS::PetalRose);
            const float Open = G.Id == EGroundSpecies::Poppy ? .7f : .2f;
            for (int32 K = 0; K < 4; ++K)
            {
                const float A = 2.f * PI * K / 4.f;
                const FVector3f D(FMath::Cos(A), FMath::Sin(A), 0.f);
                const FVector3f Up = (FVector3f(0, 0, 1) + D * Open).GetSafeNormal();
                FloraOrientedBox(B, C + D * 2.5f + Up * 4.f, FVector3f(-D.Y, D.X, 0.f), D, Up, FVector3f(3.f, .6f, 4.f));
            }
            if (G.Id == EGroundSpecies::Poppy)
            {
                B.Surface = FloraSurface(FS::SunflowerDisk);
                FloraBox(B, C, FVector3f(2.f, 2.f, 3.f));
            }
        }
        break;
    }
    case EGroundSpecies::AlpineFlower:
    {
        B.Surface = FloraSurface(FS::Moss);
        FloraBox(B, FVector3f(0, 0, -2), FVector3f(28.f, 24.f, H * .55f));
        B.Surface = FloraSurface(FS::PetalAlpine);
        for (int32 K = 0; K < 5; ++K)
        {
            const FVector3f P((FloraHash01(Seed, K, 7, 27) - .5f) * 20.f, (FloraHash01(Seed, K, 8, 28) - .5f) * 18.f, H * .55f);
            FloraBox(B, P, FVector3f(6.f, 2.f, 1.5f));
            FloraBox(B, P, FVector3f(2.f, 6.f, 1.5f));
        }
        break;
    }
    default:
        break;
    }
    if (!G.bFoliageMaterial)
    {
        FloraHeightFraction(B, First, H);
    }
}

// ---------------------------------------------------------------- placement

uint64 FFloraPlacer::MakeId(FIntPoint C, EFloraSpecies Species)
{
    return (static_cast<uint64>(static_cast<uint32>(C.X) & 0xFFFFFFFu) << 36) | (static_cast<uint64>(static_cast<uint32>(C.Y) & 0xFFFFFFFu) << 8) | static_cast<uint8>(Species);
}

FIntPoint FFloraPlacer::CellOf(uint64 Id)
{
    auto Signed = [](uint32 V) { return static_cast<int32>(V << 4) >> 4; }; // 28-bit sign extension
    return FIntPoint(Signed(static_cast<uint32>(Id >> 36) & 0xFFFFFFFu), Signed(static_cast<uint32>(Id >> 8) & 0xFFFFFFFu));
}

bool FFloraPlacer::TreeAt(const FAethraWorldGen& Gen, FIntPoint C, FFloraTree& Out)
{
    const int32 Seed = Gen.GetSeed();
    const FVector2D P(C.X * Cell + FloraHash01(Seed, C.X, C.Y, 1) * Cell, C.Y * Cell + FloraHash01(Seed, C.X, C.Y, 2) * Cell);
    const FWorldSample S = Gen.Sample(P);
    if (S.IsWater() || S.WaterDistance < 2.5f)
    {
        return false;
    }
    for (const FSettlementPad& Pad : Gen.GetPads())
    {
        if (FVector2D::Distance(P, Pad.Center) < Pad.Radius + 12.0)
        {
            return false;
        }
    }
    float Density = 0.f;
    switch (S.Biome)
    {
    case FB::Forest: Density = .2f + .7f * S.ForestDensity; break;
    case FB::Plains: Density = .035f; break;
    case FB::Meadow: Density = .025f; break;
    case FB::RiverBank: Density = .14f; break;
    case FB::Wetland: Density = .08f; break;
    case FB::Beach: Density = .03f; break;
    case FB::Mountain: Density = S.Height < 280.f ? .1f + .45f * S.ForestDensity : 0.f; break;
    case FB::SnowPeak: Density = .01f; break;
    default: break;
    }
    // Forest clearings: open glades inside the woods.
    if (S.Biome == FB::Forest)
    {
        Density *= FMath::Clamp((.74f - FFloraGen::Noise(Seed, P, 160.0, 7u)) / .06f, .08f, 1.f);
    }
    if (FloraHash01(Seed, C.X, C.Y, 3) > Density)
    {
        return false;
    }
    // Not on steep ground (two nearby samples, which also give the slope and sun exposure).
    const float H1 = Gen.Height(P + FVector2D(1.5, 0)), H2 = Gen.Height(P + FVector2D(0, 1.5));
    if (FMath::Abs(H1 - S.Height) > 1.2f || FMath::Abs(H2 - S.Height) > 1.2f)
    {
        return false;
    }
    const FFloraEnvironment Env = FFloraEnvironment::From(S, FVector3f(-(H1 - S.Height) / 1.5f, -(H2 - S.Height) / 1.5f, 1.f).GetSafeNormal());
    // Flower fields stay open: trees only at their edges.
    const FFieldSample Field = FFloraGen::FieldAt(Seed, P, Env);
    if (Field.Strength > .15f && FloraHash01(Seed, C.X, C.Y, 8) < Field.Strength * 1.2f)
    {
        return false;
    }
    const int32 Species = FFloraRegistry::PickAt(Env, Seed, P, FloraHash01(Seed, C.X, C.Y, 4));
    if (Species < 0)
    {
        return false;
    }
    const FFloraSpecies& Def = FFloraRegistry::All()[Species];
    Out.Species = Def.Id;
    Out.Id = MakeId(C, Def.Id);
    Out.Variant = FMath::Min(static_cast<int32>(FloraHash01(Seed, C.X, C.Y, 9) * FFloraRegistry::Variants), FFloraRegistry::Variants - 1);
    Out.Position = P;
    Out.Ground = S.Height;
    Out.Scale = FMath::Lerp(Def.ScaleMin, Def.ScaleMax, FloraHash01(Seed, C.X, C.Y, 5));
    Out.Yaw = 360.f * FloraHash01(Seed, C.X, C.Y, 6);
    Out.Tint = FloraHash01(Seed, C.X, C.Y, 7) - .5f;
    return true;
}

// ---------------------------------------------------------------- felled trees

void FFloraLayer::Serialize(FArchive& Ar)
{
    uint32 M = Magic;
    int32 V = Version;
    Ar << M << V;
    if (Ar.IsLoading() && (M != Magic || V < 1 || V > Version))
    {
        Ar.SetError();
        return;
    }
    TArray<uint64> Ids = Felled.Array();
    Ids.Sort(); // the same set always writes the same bytes
    int32 Count = Ids.Num();
    Ar << Count;
    if (Ar.IsLoading() && (Count < 0 || Count > 1 << 24))
    {
        Ar.SetError();
        return;
    }
    Ids.SetNum(Count);
    for (uint64& Id : Ids)
    {
        Ar << Id;
    }
    if (Ar.IsLoading())
    {
        Felled.Reset();
        Felled.Append(Ids);
    }
}

bool FFloraLayer::SaveToFile(const FString& Path) const
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    const_cast<FFloraLayer*>(this)->Serialize(Writer);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const FString Temp = Path + TEXT(".tmp");
    return FFileHelper::SaveArrayToFile(Bytes, *Temp) && IFileManager::Get().Move(*Path, *Temp, true);
}

bool FFloraLayer::LoadFromFile(const FString& Path)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path))
    {
        return false;
    }
    FFloraLayer Loaded;
    FMemoryReader Reader(Bytes);
    Loaded.Serialize(Reader);
    if (Reader.IsError() || !Reader.AtEnd())
    {
        return false;
    }
    *this = MoveTemp(Loaded);
    return true;
}
