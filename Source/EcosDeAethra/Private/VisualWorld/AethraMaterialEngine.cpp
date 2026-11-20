#include "VisualWorld/AethraMaterialEngine.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"

namespace
{
FLinearColor Srgb(uint8 R, uint8 G, uint8 B)
{
    return FLinearColor(FColor(R, G, B));
}

// The library: original values, tuned against the approved references (saturated but natural
// greens, warm earth, cool rock, warm-white stone and terracotta for the elaborate architecture).
TArray<FAmeMaterialDef> BuildLibrary()
{
    using C = EAmeCategory;
    using P = EAmePattern;
    using S = EAmeSurface;
    TArray<FAmeMaterialDef> L;
    auto Add = [&L](S Id, const TCHAR* Name, C Cat, FLinearColor Base, FLinearColor Accent, float Var, float Rough, float Metal, P Pat, float Scale, float Wear)
    {
        L.Add({ Id, Name, Cat, Base, Accent, Var, Rough, Metal, Pat, Scale, Wear });
    };
    Add(S::Grass, TEXT("Grass"), C::Terrain, Srgb(72, 120, 38), Srgb(112, 152, 50), .35f, .86f, 0, P::Grass, 2.f, .1f);
    Add(S::Meadow, TEXT("Meadow"), C::Terrain, Srgb(88, 136, 46), Srgb(126, 160, 58), .3f, .84f, 0, P::Meadow, 2.f, .1f);
    Add(S::Dirt, TEXT("Dirt"), C::Terrain, Srgb(112, 82, 56), Srgb(84, 60, 40), .25f, .9f, 0, P::Noise, 1.5f, .2f);
    Add(S::Mud, TEXT("Mud"), C::Terrain, Srgb(74, 60, 44), Srgb(52, 44, 34), .2f, .45f, 0, P::Noise, 1.f, .1f);
    Add(S::Sand, TEXT("Sand"), C::Terrain, Srgb(214, 196, 150), Srgb(190, 168, 122), .15f, .92f, 0, P::Sand, 1.f, .1f);
    Add(S::Gravel, TEXT("Gravel"), C::Terrain, Srgb(132, 124, 112), Srgb(92, 88, 82), .2f, .88f, 0, P::Gravel, 3.f, .2f);
    Add(S::Granite, TEXT("Granite"), C::Rock, Srgb(138, 134, 128), Srgb(96, 92, 90), .2f, .8f, 0, P::Speckle, 2.f, .3f);
    Add(S::Basalt, TEXT("Basalt"), C::Rock, Srgb(62, 62, 66), Srgb(40, 40, 44), .15f, .78f, 0, P::Columns, .7f, .2f);
    Add(S::Limestone, TEXT("Limestone"), C::Rock, Srgb(196, 188, 168), Srgb(162, 154, 136), .2f, .82f, 0, P::Strata, .6f, .4f);
    Add(S::Sandstone, TEXT("Sandstone"), C::Rock, Srgb(196, 140, 92), Srgb(168, 108, 70), .25f, .86f, 0, P::Strata, .5f, .4f);
    Add(S::Marble, TEXT("Marble"), C::Rock, Srgb(228, 224, 216), Srgb(170, 168, 176), .1f, .35f, 0, P::Strata, .3f, .2f);
    Add(S::Slate, TEXT("Slate"), C::Rock, Srgb(84, 92, 102), Srgb(62, 68, 76), .15f, .7f, 0, P::Strata, 1.2f, .3f);
    Add(S::Snow, TEXT("Snow"), C::Terrain, Srgb(236, 240, 246), Srgb(196, 208, 226), .05f, .55f, 0, P::Snow, .5f, 0);
    Add(S::Moss, TEXT("Moss"), C::Vegetation, Srgb(70, 104, 40), Srgb(104, 130, 52), .3f, .9f, 0, P::Moss, 3.f, 0);
    Add(S::BarkOak, TEXT("BarkOak"), C::Vegetation, Srgb(86, 64, 44), Srgb(58, 42, 30), .2f, .9f, 0, P::Bark, 4.f, .1f);
    Add(S::BarkBirch, TEXT("BarkBirch"), C::Vegetation, Srgb(222, 218, 206), Srgb(48, 44, 40), .1f, .8f, 0, P::Bark, 3.f, .1f);
    Add(S::BarkPine, TEXT("BarkPine"), C::Vegetation, Srgb(98, 62, 40), Srgb(70, 46, 32), .2f, .88f, 0, P::Bark, 3.f, .1f);
    Add(S::LeavesBroad, TEXT("LeavesBroad"), C::Vegetation, Srgb(64, 120, 40), Srgb(118, 168, 56), .35f, .75f, 0, P::Leaves, 2.f, 0);
    Add(S::LeavesNeedle, TEXT("LeavesNeedle"), C::Vegetation, Srgb(36, 82, 46), Srgb(58, 108, 56), .25f, .8f, 0, P::Leaves, 3.f, 0);
    Add(S::LeavesBirch, TEXT("LeavesBirch"), C::Vegetation, Srgb(110, 160, 60), Srgb(170, 196, 80), .3f, .72f, 0, P::Leaves, 2.f, 0);
    Add(S::Reed, TEXT("Reed"), C::Vegetation, Srgb(132, 150, 70), Srgb(184, 170, 100), .3f, .8f, 0, P::Grass, 2.f, 0);
    Add(S::WoodPlank, TEXT("WoodPlank"), C::Construction, Srgb(150, 104, 64), Srgb(110, 74, 44), .15f, .7f, 0, P::Planks, 4.f, .4f);
    Add(S::CutStone, TEXT("CutStone"), C::Construction, Srgb(214, 202, 180), Srgb(150, 140, 124), .12f, .78f, 0, P::CutStone, 1.5f, .5f);
    Add(S::Brick, TEXT("Brick"), C::Construction, Srgb(166, 92, 66), Srgb(196, 184, 164), .12f, .82f, 0, P::Brick, 4.f, .4f);
    Add(S::Mortar, TEXT("Mortar"), C::Construction, Srgb(196, 188, 170), Srgb(170, 162, 146), .08f, .9f, 0, P::Noise, 4.f, .2f);
    Add(S::RoofTile, TEXT("RoofTile"), C::Construction, Srgb(184, 92, 52), Srgb(140, 66, 40), .15f, .6f, 0, P::Tiles, 3.f, .3f);
    Add(S::Ceramic, TEXT("Ceramic"), C::Construction, Srgb(40, 92, 160), Srgb(232, 220, 190), .08f, .25f, 0, P::Tiles, 4.f, .1f);
    Add(S::Plaster, TEXT("Plaster"), C::Construction, Srgb(232, 222, 198), Srgb(204, 190, 160), .1f, .88f, 0, P::Plaster, 1.f, .4f);
    Add(S::Iron, TEXT("Iron"), C::Construction, Srgb(92, 94, 98), Srgb(120, 72, 46), .1f, .45f, 1.f, P::Metal, 2.f, .5f);
    Add(S::Bronze, TEXT("Bronze"), C::Construction, Srgb(176, 124, 64), Srgb(84, 140, 120), .1f, .35f, 1.f, P::Metal, 2.f, .5f);
    Add(S::OreCopper, TEXT("OreCopper"), C::Resource, Srgb(120, 116, 110), Srgb(196, 112, 60), .15f, .7f, .3f, P::Ore, 3.f, .1f);
    Add(S::OreIron, TEXT("OreIron"), C::Resource, Srgb(118, 112, 106), Srgb(150, 86, 62), .15f, .72f, .2f, P::Ore, 3.f, .1f);
    Add(S::Crystal, TEXT("Crystal"), C::Resource, Srgb(140, 210, 230), Srgb(220, 250, 255), .1f, .15f, 0, P::Ore, 1.f, 0);
    return L;
}

uint32 Mix(uint32 H)
{
    H ^= H >> 16;
    H *= 0x7feb352dU;
    H ^= H >> 15;
    H *= 0x846ca68bU;
    H ^= H >> 16;
    return H;
}

float Hash01(int32 Seed, FIntPoint Cell, uint32 Salt)
{
    return (Mix(static_cast<uint32>(Seed) ^ Mix(static_cast<uint32>(Cell.X) * 0x9E3779B1U ^ Mix(static_cast<uint32>(Cell.Y) + Salt))) & 0xFFFFFF) / 16777215.f;
}
}

const TArray<FAmeMaterialDef>& FAmeRegistry::All()
{
    static const TArray<FAmeMaterialDef> Library = BuildLibrary();
    return Library;
}

const FAmeMaterialDef& FAmeRegistry::Get(EAmeSurface Id)
{
    return All()[FMath::Clamp(static_cast<int32>(Id), 0, All().Num() - 1)];
}

const FAmeMaterialDef* FAmeRegistry::FindByName(const FString& Name)
{
    return All().FindByPredicate([&Name](const FAmeMaterialDef& D) { return Name.Equals(D.Name, ESearchCase::IgnoreCase); });
}

TArray<FString> FAmeRegistry::Validate()
{
    TArray<FString> Problems;
    const TArray<FAmeMaterialDef>& L = All();
    if (L.Num() != static_cast<int32>(EAmeSurface::Count))
    {
        Problems.Add(FString::Printf(TEXT("%d surfaces for %d ids"), L.Num(), static_cast<int32>(EAmeSurface::Count)));
    }
    if (L.Num() > PaletteWidth)
    {
        Problems.Add(TEXT("more surfaces than palette columns"));
    }
    TSet<FString> Names;
    for (int32 I = 0; I < L.Num(); ++I)
    {
        const FAmeMaterialDef& D = L[I];
        if (static_cast<int32>(D.Id) != I)
        {
            Problems.Add(FString::Printf(TEXT("%s is at %d but has id %d"), D.Name, I, static_cast<int32>(D.Id)));
        }
        bool bDuplicate = false;
        Names.Add(D.Name, &bDuplicate);
        if (bDuplicate)
        {
            Problems.Add(FString::Printf(TEXT("duplicate name %s"), D.Name));
        }
        auto InUnit = [](float V) { return V >= 0.f && V <= 1.f; };
        if (!InUnit(D.Roughness) || !InUnit(D.Metallic) || !InUnit(D.Variation) || !InUnit(D.Wear) || D.PatternScale <= 0.f
            || !InUnit(D.Base.R) || !InUnit(D.Base.G) || !InUnit(D.Base.B) || !InUnit(D.Accent.R) || !InUnit(D.Accent.G) || !InUnit(D.Accent.B)
            || D.Pattern >= EAmePattern::Count)
        {
            Problems.Add(FString::Printf(TEXT("%s has a parameter out of range"), D.Name));
        }
    }
    return Problems;
}

UTexture2D* FAmeRegistry::CreatePalette(UObject* Outer)
{
    UTexture2D* Texture = UTexture2D::CreateTransient(PaletteWidth, PaletteRows, PF_A32B32G32R32F, TEXT("AmePalette"));
    if (!Texture)
    {
        return nullptr;
    }
    Texture->Filter = TF_Nearest;
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_HDR;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    FLinearColor* Texels = static_cast<FLinearColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memzero(Texels, sizeof(FLinearColor) * PaletteWidth * PaletteRows);
    for (const FAmeMaterialDef& D : All())
    {
        const int32 X = static_cast<int32>(D.Id);
        Texels[0 * PaletteWidth + X] = FLinearColor(D.Base.R, D.Base.G, D.Base.B, D.Variation);
        Texels[1 * PaletteWidth + X] = FLinearColor(D.Accent.R, D.Accent.G, D.Accent.B, D.Wear);
        Texels[2 * PaletteWidth + X] = FLinearColor(D.Roughness, D.Metallic, static_cast<float>(D.Pattern), D.PatternScale);
        Texels[3 * PaletteWidth + X] = FLinearColor(static_cast<float>(D.Category), 0.f, 0.f, 1.f);
    }
    Mip.BulkData.Unlock();
    Texture->UpdateResource();
    return Texture;
}

EAmeSurface FAmeSelector::RockSurface(EAmeRock Rock)
{
    switch (Rock)
    {
    case EAmeRock::Basalt: return EAmeSurface::Basalt;
    case EAmeRock::Limestone: return EAmeSurface::Limestone;
    case EAmeRock::Sandstone: return EAmeSurface::Sandstone;
    case EAmeRock::Slate: return EAmeSurface::Slate;
    case EAmeRock::Marble: return EAmeSurface::Marble;
    default: return EAmeSurface::Granite;
    }
}

FAmeChoice FAmeSelector::Terrain(const FAmeEnvironment& Env, int32 Seed, FIntPoint Cell)
{
    using S = EAmeSurface;
    FAmeChoice C;
    const float Patch = Hash01(Seed, FIntPoint(Cell.X >> 3, Cell.Y >> 3), 17); // ~8-cell patches
    const S Rock = RockSurface(Env.Rock);

    // 1. Steep ground shows bare rock; the transition band blends soil into rock.
    const float SteepStart = Env.Biome == EAmeBiome::Mountain || Env.Biome == EAmeBiome::SnowPeak ? .28f : .38f;
    // 2. Cold ground (snow line falls with temperature) keeps snow on gentle slopes only.
    const float SnowAmount = FMath::Clamp((-Env.Temperature + 1.f) / 4.f, 0.f, 1.f) * FMath::Clamp(1.f - (Env.Slope - .45f) / .2f, 0.f, 1.f);
    // 3. The base soil of the biome.
    S Soil = S::Grass;
    S SoilAlt = S::Grass;
    switch (Env.Biome)
    {
    case EAmeBiome::Meadow: Soil = S::Meadow; SoilAlt = S::Grass; break;
    case EAmeBiome::Forest: Soil = S::Grass; SoilAlt = S::Moss; break;
    case EAmeBiome::Mountain: Soil = S::Gravel; SoilAlt = S::Grass; break;
    case EAmeBiome::SnowPeak: Soil = S::Snow; SoilAlt = S::Gravel; break;
    case EAmeBiome::RiverBank: Soil = Env.Moisture > .75f ? S::Mud : S::Gravel; SoilAlt = S::Sand; break;
    case EAmeBiome::Beach: Soil = S::Sand; SoilAlt = S::Gravel; break;
    case EAmeBiome::Wetland: Soil = S::Mud; SoilAlt = S::Moss; break;
    case EAmeBiome::Desert: Soil = S::Sand; SoilAlt = S::Sandstone; break;
    case EAmeBiome::Savanna: Soil = S::Dirt; SoilAlt = S::Grass; break;
    case EAmeBiome::Tundra: Soil = S::Gravel; SoilAlt = S::Moss; break;
    case EAmeBiome::Volcanic: Soil = S::Basalt; SoilAlt = S::Gravel; break;
    default: Soil = Env.Moisture > .62f ? S::Meadow : S::Grass; SoilAlt = Env.Moisture < .3f ? S::Dirt : S::Meadow; break;
    }
    // Right at the water: wet edges are sand, gravel or mud, whatever the biome.
    if (Env.WaterDistance < 3.f && Env.Biome != EAmeBiome::SnowPeak)
    {
        Soil = Env.Biome == EAmeBiome::Beach ? S::Sand : Env.Moisture > .7f ? S::Mud : S::Gravel;
        SoilAlt = S::Sand;
    }
    C.Primary = Soil;
    C.Secondary = SoilAlt;
    // Patches of the alternative soil, more of them where the climate sits between the two.
    C.Blend = FMath::Clamp((Patch - .62f) * 2.5f, 0.f, 1.f) * .7f;

    if (SnowAmount > .02f)
    {
        C.Secondary = C.Primary;
        C.Primary = S::Snow;
        C.Blend = 1.f - SnowAmount;
    }
    const float Steep = FMath::Clamp((Env.Slope - SteepStart) / .14f, 0.f, 1.f);
    if (Steep > 0.f)
    {
        C.Secondary = Rock;
        C.Blend = FMath::Max(C.Blend, Steep);
    }
    // Weather and wear do not change the surface, only the shader's wetness/wear inputs.
    return C;
}

EAmeSurface FAmeSelector::Building(EAmeCulture Culture, EAmePart Part, EAmeRock LocalRock, float Wealth)
{
    using S = EAmeSurface;
    const bool bRich = Wealth >= .6f;
    switch (Culture)
    {
    case EAmeCulture::RiverTimber:
        switch (Part)
        {
        case EAmePart::Wall: return bRich ? S::Plaster : S::WoodPlank;
        case EAmePart::Roof: return bRich ? S::RoofTile : S::Reed;
        case EAmePart::Foundation: return RockSurface(LocalRock);
        case EAmePart::Column: return S::BarkOak;
        default: return S::WoodPlank;
        }
    case EAmeCulture::HighlandSlate:
        switch (Part)
        {
        case EAmePart::Roof: return S::Slate;
        case EAmePart::Trim: return bRich ? S::Bronze : S::Iron;
        case EAmePart::Floor:
        case EAmePart::Deck: return S::WoodPlank;
        default: return RockSurface(LocalRock);
        }
    default: // EldenStone: warm cut stone, plaster, terracotta and blue ceramic for the rich
        switch (Part)
        {
        case EAmePart::Wall: return bRich ? S::Plaster : S::CutStone;
        case EAmePart::Foundation: return S::CutStone;
        case EAmePart::Roof: return bRich ? S::Ceramic : S::RoofTile;
        case EAmePart::Trim: return bRich ? S::Marble : S::CutStone;
        case EAmePart::Column: return bRich ? S::Marble : S::CutStone;
        case EAmePart::Floor: return S::CutStone;
        case EAmePart::Deck: return S::CutStone;
        default: return S::CutStone;
        }
    }
}

const TCHAR* FAmeSelector::BiomeName(EAmeBiome Biome)
{
    static const TCHAR* Names[] = { TEXT("Planície"), TEXT("Campo florido"), TEXT("Floresta"), TEXT("Montanha"), TEXT("Pico nevado"), TEXT("Margem de rio"),
        TEXT("Praia"), TEXT("Área alagada"), TEXT("Deserto"), TEXT("Savana"), TEXT("Tundra"), TEXT("Floresta tropical"), TEXT("Vulcânico"), TEXT("Caverna") };
    static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EAmeBiome::Count));
    return Names[FMath::Clamp(static_cast<int32>(Biome), 0, UE_ARRAY_COUNT(Names) - 1)];
}
