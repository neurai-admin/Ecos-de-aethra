#include "VisualWorld/AethraRuinGen.h"

#include "VisualWorld/AethraCaveGen.h"
#include "VisualWorld/AethraWorldGen.h"

// Names prefixed (Ruin...) because the unity build merges this file with others.
namespace
{
using RS = EAmeSurface;

uint32 RuinMix(uint32 H)
{
    H ^= H >> 16;
    H *= 0x7feb352dU;
    H ^= H >> 15;
    H *= 0x846ca68bU;
    H ^= H >> 16;
    return H;
}

float RuinHash(int32 Seed, int32 A, int32 B, uint32 Salt)
{
    return (RuinMix(static_cast<uint32>(Seed) ^ RuinMix(static_cast<uint32>(A) * 0x9E3779B1U ^ RuinMix(static_cast<uint32>(B) * 0x85EBCA77U + Salt))) & 0xFFFFFF) / 16777215.f;
}

struct FRuinPalette
{
    EAmeSurface Wall, Column, Roof, Accent, Floor;
};

FRuinPalette RuinPaletteOf(ERuinCulture Culture)
{
    switch (Culture)
    {
    case ERuinCulture::Skarn: return { RS::Basalt, RS::Slate, RS::Slate, RS::Iron, RS::Basalt };
    case ERuinCulture::Mirethi: return { RS::Sandstone, RS::Sandstone, RS::Ceramic, RS::Bronze, RS::Sandstone };
    default: return { RS::CutStone, RS::Marble, RS::Marble, RS::Bronze, RS::Limestone };
    }
}

// Builds pieces in the site's frame (metres; x forward, z up from the anchor) with decay.
struct FRuinBuilder
{
    const FRuinSite& Site;
    int32 Seed;
    TArray<FRuinPiece>& Out;
    int32 Counter = 0;

    float Rand(uint32 Salt) { return RuinHash(Seed, Site.Id * 977 + Counter, static_cast<int32>(Salt), 501u); }

    // Height fraction 0 (foundations, always kept) .. 1 (tops, lost first).
    bool Keep(float Level)
    {
        ++Counter;
        return Rand(1) < Site.Preservation + (1.f - Level) * .65f;
    }

    void Add(EAhgShape Shape, EAmeSurface Surface, const FVector& PosM, const FVector& SizeM, float Yaw = 0.f, float Pitch = 0.f, float Roll = 0.f)
    {
        ++Counter;
        // Settling: old stones lean a little.
        const float Tilt = (1.f - Site.Preservation) * 4.f;
        const FQuat Local = FRotator(Pitch + (Rand(2) - .5f) * Tilt, Yaw + (Rand(3) - .5f) * Tilt * 2.f, Roll + (Rand(4) - .5f) * Tilt).Quaternion();
        const FQuat Frame = FRotator(0.f, Site.Yaw, 0.f).Quaternion();
        FRuinPiece P;
        P.Shape = Shape;
        P.Surface = Surface;
        P.Transform = FTransform(Frame * Local, Site.Anchor * 100.0 + Frame.RotateVector(PosM * 100.0), SizeM);
        Out.Add(P);
        // Moss on the top of weathered horizontal stones.
        if (Shape == EAhgShape::Block && Pitch == 0.f && Surface != RS::Moss && SizeM.X > .8f && SizeM.Y > .8f && Rand(5) < (1.f - Site.Preservation) * .7f)
        {
            FRuinPiece M = P;
            M.Surface = RS::Moss;
            M.Transform = FTransform(Frame * Local, Site.Anchor * 100.0 + Frame.RotateVector((PosM + FVector(0, 0, SizeM.Z)) * 100.0), FVector(SizeM.X * .8f, SizeM.Y * .7f, .06f));
            Out.Add(M);
        }
    }

    // A fallen column or block lying on the ground near P.
    void Debris(EAhgShape Shape, EAmeSurface Surface, const FVector& PosM, float Length, float Width)
    {
        const float Yaw = 360.f * Rand(6);
        if (Shape == EAhgShape::Column || Shape == EAhgShape::Cylinder)
        {
            Add(Shape, Surface, PosM + FVector(0, 0, Width * .5f), FVector(Width, Width, Length), Yaw, 90.f);
        }
        else
        {
            Add(Shape, Surface, PosM, FVector(Width, Width * (.6f + .5f * Rand(7)), Width * .6f), Yaw);
        }
    }

    void Column(const FRuinPalette& Pal, const FVector& At, float Height, float Width)
    {
        Add(EAhgShape::Block, Pal.Floor, At, FVector(Width * 1.4f, Width * 1.4f, .3f));
        if (Keep(.5f))
        {
            const float H = Keep(.9f) ? Height : Height * (.3f + .5f * Rand(8)); // broken shaft
            Add(EAhgShape::Column, Pal.Column, At + FVector(0, 0, .3f), FVector(Width, Width, H));
            if (H >= Height)
            {
                Add(EAhgShape::Block, Pal.Floor, At + FVector(0, 0, .3f + H), FVector(Width * 1.3f, Width * 1.3f, .25f));
            }
            else
            {
                Debris(EAhgShape::Column, Pal.Column, At + FVector(Rand(9) * 3.f - 1.5f, Rand(10) * 3.f - 1.5f, 0), Height - H, Width);
            }
        }
        else
        {
            Debris(EAhgShape::Column, Pal.Column, At + FVector(1.f, .5f, 0), Height * .7f, Width);
        }
    }
};

void RuinTemple(FRuinBuilder& B, const FRuinPalette& Pal)
{
    // A stepped platform, a colonnade, a broken cella and an altar.
    B.Add(EAhgShape::Block, Pal.Floor, FVector(0, 0, -.6f), FVector(18.f, 12.f, .9f));
    B.Add(EAhgShape::Block, Pal.Floor, FVector(0, 0, .3f), FVector(16.f, 10.f, .4f));
    B.Add(EAhgShape::Stairs, Pal.Floor, FVector(9.6f, 0, -.6f), FVector(1.6f, 4.f, 1.3f), 180.f);
    for (int32 K = 0; K < 6; ++K)
    {
        for (const float Side : { -1.f, 1.f })
        {
            B.Column(Pal, FVector(-6.5f + K * 2.6f, Side * 4.2f, .7f), 5.2f, .7f);
        }
    }
    // Architrave fragments where neighbouring columns still stand.
    for (int32 K = 0; K < 3; ++K)
    {
        if (B.Keep(1.f))
        {
            B.Add(EAhgShape::Block, Pal.Wall, FVector(-5.2f + K * 5.2f, -4.2f, 6.2f), FVector(5.4f, .9f, .6f));
        }
    }
    // Cella walls at uneven heights, with a doorway.
    for (int32 K = 0; K < 5; ++K)
    {
        const float H = 1.f + 3.f * B.Rand(20 + K) * B.Site.Preservation;
        B.Add(EAhgShape::Block, Pal.Wall, FVector(-3.f + K * 1.4f, -2.f, .7f), FVector(1.4f, .6f, H));
        B.Add(EAhgShape::Block, Pal.Wall, FVector(-3.f + K * 1.4f, 2.f, .7f), FVector(1.4f, .6f, 1.f + 3.f * B.Rand(30 + K) * B.Site.Preservation));
    }
    if (B.Keep(.6f))
    {
        B.Add(EAhgShape::Arch, Pal.Wall, FVector(3.8f, 0, .7f), FVector(.6f, 3.4f, 3.4f), 90.f);
    }
    // Altar with a bronze offering bowl: the cache point.
    B.Add(EAhgShape::Block, Pal.Column, FVector(-2.f, 0, .7f), FVector(1.6f, 1.f, 1.f));
    B.Add(EAhgShape::Dome, Pal.Accent, FVector(-2.f, 0, 1.7f), FVector(.7f, .7f, .35f));
    // Rubble around.
    for (int32 K = 0; K < 6; ++K)
    {
        B.Debris(EAhgShape::Block, Pal.Wall, FVector(-9.f + 18.f * B.Rand(40 + K), -7.f + 14.f * B.Rand(50 + K), 0), 1.f, .6f + .6f * B.Rand(60 + K));
    }
}

void RuinWall(FRuinBuilder& B, const FRuinPalette& Pal)
{
    for (int32 K = 0; K < 10; ++K)
    {
        const float X = -13.5f + K * 3.f;
        if (B.Keep(.3f))
        {
            const float H = 1.f + 4.f * B.Rand(70 + K) * (.3f + B.Site.Preservation);
            B.Add(EAhgShape::Block, Pal.Wall, FVector(X, 0, -.3f), FVector(3.f, 1.4f, H));
            if (B.Keep(.95f))
            {
                B.Add(EAhgShape::HalfBlock, Pal.Wall, FVector(X, 0, -.3f + H), FVector(1.f, 1.4f, .8f)); // a merlon
            }
        }
        else
        {
            B.Debris(EAhgShape::Block, Pal.Wall, FVector(X, 2.f + 2.f * B.Rand(80 + K), 0), 1.f, .9f);
        }
    }
    B.Add(EAhgShape::Arch, Pal.Wall, FVector(.5f, 0, -.3f), FVector(3.f, 1.6f, 4.5f)); // the gate
    B.Add(EAhgShape::Cylinder, Pal.Wall, FVector(-15.f, 0, -.3f), FVector(3.f, 3.f, 3.f + 4.f * B.Site.Preservation));
}

void RuinTower(FRuinBuilder& B, const FRuinPalette& Pal)
{
    const float H = 4.f + 10.f * B.Site.Preservation;
    B.Add(EAhgShape::Cylinder, Pal.Wall, FVector(0, 0, -.5f), FVector(7.f, 7.f, H));
    B.Add(EAhgShape::Cylinder, Pal.Floor, FVector(0, 0, -.5f), FVector(8.f, 8.f, 1.2f));
    if (B.Keep(.8f))
    {
        B.Add(EAhgShape::Cylinder, Pal.Column, FVector(0, 0, H - .5f), FVector(7.6f, 7.6f, .5f));
    }
    // The fallen top: drums and the dome lying beside it.
    for (int32 K = 0; K < 4; ++K)
    {
        B.Debris(EAhgShape::Cylinder, Pal.Wall, FVector(6.f + K * 2.2f, -3.f + B.Rand(90 + K) * 6.f, 0), 2.f, 3.f + B.Rand(95 + K));
    }
    B.Add(EAhgShape::Dome, Pal.Roof, FVector(12.f, 4.f, -.4f), FVector(6.f, 6.f, 3.f), 0.f, 160.f);
    B.Add(EAhgShape::Arch, Pal.Wall, FVector(3.4f, 0, -.5f), FVector(1.2f, 2.f, 3.f), 90.f);
}

void RuinShrine(FRuinBuilder& B, const FRuinPalette& Pal)
{
    B.Add(EAhgShape::Block, Pal.Floor, FVector(0, 0, -.4f), FVector(6.f, 6.f, .8f));
    for (int32 K = 0; K < 4; ++K)
    {
        B.Column(Pal, FVector((K & 1) ? 2.f : -2.f, (K & 2) ? 2.f : -2.f, .4f), 3.2f, .5f);
    }
    if (B.Keep(.9f))
    {
        B.Add(EAhgShape::Block, Pal.Wall, FVector(0, 0, 3.9f), FVector(5.f, 5.f, .4f));
        B.Add(EAhgShape::Dome, Pal.Roof, FVector(0, 0, 4.3f), FVector(4.f, 4.f, 2.f));
    }
    B.Add(EAhgShape::Sphere, Pal.Accent, FVector(0, 0, .4f), FVector(.8f, .8f, .8f)); // the offering stone
}

void RuinStatue(FRuinBuilder& B, const FRuinPalette& Pal)
{
    B.Add(EAhgShape::Block, Pal.Floor, FVector(0, 0, -.3f), FVector(4.f, 4.f, 1.6f));
    B.Add(EAhgShape::Block, Pal.Column, FVector(-.6f, 0, 1.3f), FVector(.8f, .9f, 2.4f)); // legs
    B.Add(EAhgShape::Block, Pal.Column, FVector(.6f, 0, 1.3f), FVector(.8f, .9f, 2.4f * (.4f + .6f * B.Site.Preservation)));
    B.Add(EAhgShape::Block, Pal.Column, FVector(4.f, 1.f, 0), FVector(3.f, 1.6f, 1.2f), 25.f); // the fallen torso
    B.Add(EAhgShape::Sphere, Pal.Column, FVector(6.5f, -1.f, 0), FVector(1.3f, 1.3f, 1.3f));  // and the head
    B.Add(EAhgShape::Column, Pal.Accent, FVector(3.f, -2.5f, .3f), FVector(.25f, .25f, 4.f), 70.f, 90.f); // a bronze spear
}

void RuinCaveHall(FRuinBuilder& B, const FRuinPalette& Pal)
{
    // A paved hall cut into the rock: columns up to the ceiling, an altar, shelves of an old library.
    B.Add(EAhgShape::Block, Pal.Floor, FVector(0, 0, -.3f), FVector(12.f, 7.f, .4f));
    for (int32 K = 0; K < 4; ++K)
    {
        for (const float Side : { -1.f, 1.f })
        {
            B.Column(Pal, FVector(-4.5f + K * 3.f, Side * 2.6f, .1f), 4.5f, .6f);
        }
    }
    B.Add(EAhgShape::Block, Pal.Column, FVector(5.f, 0, .1f), FVector(1.2f, 2.4f, 1.1f));
    B.Add(EAhgShape::Wedge, Pal.Accent, FVector(5.f, 0, 1.2f), FVector(1.f, 2.f, .5f));
    for (int32 K = 0; K < 3; ++K)
    {
        if (B.Keep(.5f))
        {
            B.Add(EAhgShape::Block, RS::WoodPlank, FVector(-5.f + K * 2.f, -3.4f, .1f), FVector(1.8f, .5f, 2.2f)); // shelves
        }
    }
    B.Add(EAhgShape::Arch, Pal.Wall, FVector(-6.4f, 0, .1f), FVector(.8f, 3.f, 3.6f), 90.f);
}

void RuinCrypt(FRuinBuilder& B, const FRuinPalette& Pal)
{
    B.Add(EAhgShape::Block, Pal.Floor, FVector(0, 0, -.3f), FVector(9.f, 6.f, .4f));
    for (int32 K = 0; K < 4; ++K)
    {
        const FVector At(-3.f + K * 2.f, (K & 1) ? 1.6f : -1.6f, .1f);
        B.Add(EAhgShape::Block, Pal.Wall, At, FVector(1.f, 2.2f, .8f)); // sarcophagus
        if (B.Keep(.7f))
        {
            B.Add(EAhgShape::Wedge, Pal.Column, At + FVector(0, 0, .8f), FVector(1.f, 2.2f, .3f));
        }
        else
        {
            B.Debris(EAhgShape::Block, Pal.Column, At + FVector(.8f, 0, 0), 1.f, .8f); // the lid pushed off
        }
    }
    B.Add(EAhgShape::Arch, Pal.Wall, FVector(4.6f, 0, .1f), FVector(.8f, 2.4f, 3.f), 90.f);
}

void RuinMine(FRuinBuilder& B, const FRuinPalette& Pal)
{
    // Timber frames along a gallery, a rail of planks, crates and a collapsed frame.
    for (int32 K = 0; K < 4; ++K)
    {
        const float X = -6.f + K * 4.f;
        if (B.Keep(.4f))
        {
            B.Add(EAhgShape::Block, RS::WoodPlank, FVector(X, -1.6f, 0), FVector(.35f, .35f, 3.f));
            B.Add(EAhgShape::Block, RS::WoodPlank, FVector(X, 1.6f, 0), FVector(.35f, .35f, 3.f));
            B.Add(EAhgShape::Block, RS::WoodPlank, FVector(X, 0, 3.f), FVector(.4f, 3.6f, .35f));
        }
        else
        {
            B.Debris(EAhgShape::Column, RS::WoodPlank, FVector(X, 0, 0), 3.f, .35f);
        }
    }
    B.Add(EAhgShape::Block, RS::WoodPlank, FVector(0, 0, 0), FVector(14.f, .9f, .12f));
    B.Add(EAhgShape::Block, RS::WoodPlank, FVector(-2.f, 1.f, 0), FVector(.9f, .9f, .9f), 20.f);
    B.Add(EAhgShape::Block, RS::OreCopper, FVector(3.f, -.9f, 0), FVector(1.f, .8f, .6f), -15.f); // ore left behind
    B.Add(EAhgShape::Block, Pal.Accent, FVector(1.f, 1.1f, 0), FVector(.6f, .6f, .5f));
}
}

FAethraRuinGen::FAethraRuinGen(const FAethraWorldGen& Gen, const FAethraCaveGen* Caves, const FVector2D& Spawn)
    : Seed(Gen.GetSeed())
{
    auto CultureFor = [](EAmeRock Rock)
    {
        switch (Rock)
        {
        case EAmeRock::Basalt: case EAmeRock::Slate: return ERuinCulture::Skarn;
        case EAmeRock::Sandstone: case EAmeRock::Granite: return ERuinCulture::Mirethi;
        default: return ERuinCulture::Aldarim;
        }
    };
    auto Finish = [this](FRuinSite& S, int32 Salt)
    {
        S.Id = RuinSites.Num();
        S.Yaw = 360.f * RuinHash(Seed, S.Id, Salt, 11u);
        S.AgeYears = 400.f + 2600.f * RuinHash(Seed, S.Id, Salt, 12u);
        const float R = RuinHash(Seed, S.Id, Salt, 13u);
        S.Rarity = R > .97f ? 3 : R > .85f ? 2 : R > .6f ? 1 : 0;
        S.Importance = FMath::Clamp(.25f + .25f * S.Rarity + .2f * RuinHash(Seed, S.Id, Salt, 14u), 0.f, 1.f);
        S.Lore = { FName(*FString::Printf(TEXT("culture:%s"), CultureName(S.Culture))), FName(*FString::Printf(TEXT("kind:%s"), KindName(S.Kind))),
            FName(*FString::Printf(TEXT("era:%d"), FMath::FloorToInt(S.AgeYears / 500.f))) };
        S.Cache = S.Anchor + FVector(0, 0, 1.2f);
        RuinSites.Add(S);
    };
    auto SuitableSurface = [&Gen](const FVector2D& P, float MaxSlope)
    {
        const FWorldSample S = Gen.Sample(P);
        if (S.IsWater() || S.WaterDistance < 12.f || S.Height < 2.f)
        {
            return false;
        }
        for (const FSettlementPad& Pad : Gen.GetPads())
        {
            if (FVector2D::Distance(P, Pad.Center) < Pad.Radius + 50.0)
            {
                return false;
            }
        }
        const float Slope = FMath::Abs(Gen.Height(P + FVector2D(8, 0)) - Gen.Height(P - FVector2D(8, 0))) / 16.f
            + FMath::Abs(Gen.Height(P + FVector2D(0, 8)) - Gen.Height(P - FVector2D(0, 8))) / 16.f;
        return Slope < MaxSlope;
    };
    // 1. Demonstration temple near the spawn: flat open ground, visible from the settlement.
    for (int32 K = 0; K < 96; ++K)
    {
        const float A = 2.f * PI * RuinHash(Seed, K, 1, 21u), R = 220.f + 380.f * RuinHash(Seed, K, 2, 22u);
        const FVector2D P = Spawn + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
        if (!SuitableSurface(P, .12f) || (Caves && Caves->DemoSystem() && FVector2D::Distance(P, FVector2D(Caves->DemoSystem()->Entrance)) < 60.0))
        {
            continue;
        }
        FRuinSite S;
        S.Kind = ERuinKind::Temple;
        S.Culture = ERuinCulture::Aldarim;
        S.Anchor = FVector(P.X, P.Y, Gen.Height(P));
        S.Radius = 14.f;
        S.Preservation = .5f;
        Finish(S, 1);
        RuinSites.Last().Rarity = 2;
        break;
    }
    // 2. Demonstration hall in the demo cave: on the floor of its first chamber.
    if (Caves && Caves->DemoSystem() && Caves->DemoSystem()->Chambers.Num())
    {
        const FCaveChamber& Ch = Caves->DemoSystem()->Chambers[0];
        FVector Floor = Ch.Center;
        for (float Z = 0.f; Z < Ch.Radii.Z * 2.f && Caves->IsAir(Floor - FVector(0, 0, .25f)); Z += .25f)
        {
            Floor.Z -= .25f;
        }
        FRuinSite S;
        S.Kind = ERuinKind::CaveHall;
        S.Culture = CultureFor(Caves->DemoSystem()->Rock);
        S.Anchor = Floor;
        S.Radius = 7.f;
        S.Preservation = .55f;
        S.bUnderground = true;
        S.CaveSystem = 0;
        Finish(S, 2);
        RuinSites.Last().Yaw = FMath::RadiansToDegrees(FMath::Atan2(Caves->DemoSystem()->Inward.Y, Caves->DemoSystem()->Inward.X));
    }
    // 3. Sparse sites across the domain, the kind and culture from the place.
    const double Cell = 640.0;
    for (int32 Y = -6; Y <= 6; ++Y)
    {
        for (int32 X = -6; X <= 6; ++X)
        {
            if (RuinHash(Seed, X, Y, 31u) > .28f)
            {
                continue;
            }
            const FVector2D P((X + .15 + .7 * RuinHash(Seed, X, Y, 32u)) * Cell, (Y + .15 + .7 * RuinHash(Seed, X, Y, 33u)) * Cell);
            if (!SuitableSurface(P, .25f) || FVector2D::Distance(P, Spawn) < 200.0)
            {
                continue;
            }
            const FWorldSample W = Gen.Sample(P);
            const float Roll = RuinHash(Seed, X, Y, 34u);
            FRuinSite S;
            S.Culture = CultureFor(W.Rock);
            if (W.Height > 170.f)
            {
                S.Kind = Roll < .6f ? ERuinKind::FallenTower : ERuinKind::BrokenWall; // watchposts and forts on the heights
                S.Culture = ERuinCulture::Skarn;
            }
            else if (W.Biome == EAmeBiome::Forest)
            {
                S.Kind = Roll < .7f ? ERuinKind::Shrine : ERuinKind::StatueRemains; // overgrown shrines
            }
            else if (W.WaterDistance < 60.f)
            {
                S.Kind = Roll < .5f ? ERuinKind::BrokenWall : ERuinKind::FallenTower; // old river forts
            }
            else
            {
                S.Kind = Roll < .4f ? ERuinKind::Temple : Roll < .7f ? ERuinKind::StatueRemains : ERuinKind::BrokenWall;
            }
            S.Anchor = FVector(P.X, P.Y, Gen.Height(P));
            S.Radius = S.Kind == ERuinKind::Temple ? 14.f : S.Kind == ERuinKind::BrokenWall ? 17.f : 10.f;
            S.Preservation = .15f + .7f * RuinHash(Seed, X, Y, 35u);
            Finish(S, 100 + Y * 13 + X);
        }
    }
    // 4. Underground: some cave systems hold ruins (deeper caves, older and rarer).
    if (Caves)
    {
        for (const FCaveSystem& Sys : Caves->Systems())
        {
            if (Sys.Id == 0 || Sys.Chambers.Num() == 0 || RuinHash(Seed, Sys.Id, 0, 41u) > .5f)
            {
                continue;
            }
            const FCaveChamber& Ch = Sys.Chambers.Last();
            FVector Floor = Ch.Center;
            for (float Z = 0.f; Z < Ch.Radii.Z * 2.f && Caves->IsAir(Floor - FVector(0, 0, .25f)); Z += .25f)
            {
                Floor.Z -= .25f;
            }
            FRuinSite S;
            S.Kind = Sys.Kind == ECaveKind::Natural ? ERuinKind::AbandonedMine : Sys.Kind == ECaveKind::Dark ? ERuinKind::Crypt : ERuinKind::CaveHall;
            S.Culture = CultureFor(Sys.Rock);
            S.Anchor = Floor;
            S.Radius = 7.f;
            S.Preservation = .2f + .5f * RuinHash(Seed, Sys.Id, 1, 42u);
            S.bUnderground = true;
            S.CaveSystem = Sys.Id;
            Finish(S, 500 + Sys.Id);
        }
    }
}

const FRuinSite* FAethraRuinGen::SurfaceSiteAt(const FVector2D& P) const
{
    for (const FRuinSite& S : RuinSites)
    {
        if (!S.bUnderground && FVector2D::Distance(P, FVector2D(S.Anchor)) < S.Radius)
        {
            return &S;
        }
    }
    return nullptr;
}

void FAethraRuinGen::Build(const FRuinSite& Site, int32 Seed, TArray<FRuinPiece>& Out)
{
    FRuinBuilder B{ Site, Seed, Out };
    const FRuinPalette Pal = RuinPaletteOf(Site.Culture);
    switch (Site.Kind)
    {
    case ERuinKind::Temple: RuinTemple(B, Pal); break;
    case ERuinKind::BrokenWall: RuinWall(B, Pal); break;
    case ERuinKind::FallenTower: RuinTower(B, Pal); break;
    case ERuinKind::Shrine: RuinShrine(B, Pal); break;
    case ERuinKind::StatueRemains: RuinStatue(B, Pal); break;
    case ERuinKind::CaveHall: RuinCaveHall(B, Pal); break;
    case ERuinKind::Crypt: RuinCrypt(B, Pal); break;
    case ERuinKind::AbandonedMine: RuinMine(B, Pal); break;
    default: break;
    }
}

const TCHAR* FAethraRuinGen::KindName(ERuinKind Kind)
{
    static const TCHAR* Names[] = { TEXT("templo"), TEXT("muralha"), TEXT("torre-caída"), TEXT("santuário"), TEXT("estátua"), TEXT("salão-subterrâneo"), TEXT("cripta"), TEXT("mina-abandonada") };
    return Names[FMath::Clamp(static_cast<int32>(Kind), 0, 7)];
}

const TCHAR* FAethraRuinGen::CultureName(ERuinCulture Culture)
{
    static const TCHAR* Names[] = { TEXT("Aldarim"), TEXT("Skarn"), TEXT("Mirethi") };
    return Names[FMath::Clamp(static_cast<int32>(Culture), 0, 2)];
}
