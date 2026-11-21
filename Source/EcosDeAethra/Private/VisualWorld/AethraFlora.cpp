#include "VisualWorld/AethraFlora.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "VisualWorld/AethraHybridGeometry.h"
#include "VisualWorld/AethraWorldGen.h"

namespace
{
using B = EAmeBiome;

uint32 Mix(uint32 H)
{
    H ^= H >> 16;
    H *= 0x7feb352dU;
    H ^= H >> 15;
    H *= 0x846ca68bU;
    H ^= H >> 16;
    return H;
}

float Hash01(int32 Seed, int32 X, int32 Y, uint32 Salt)
{
    return (Mix(static_cast<uint32>(Seed) ^ Mix(static_cast<uint32>(X) * 0x9E3779B1U ^ Mix(static_cast<uint32>(Y) * 0x85EBCA77U + Salt))) & 0xFFFFFF) / 16777215.f;
}

float SurfaceId(EAmeSurface S)
{
    return static_cast<float>(S);
}

TArray<FFloraSpecies> MakeSpecies()
{
    TArray<FFloraSpecies> L;
    auto Add = [&L](EFloraSpecies Id, const TCHAR* Name, EFloraForm Form, EAmeSurface Bark, EAmeSurface Leaves,
        float TrunkH, float TrunkW, float CrownW, float CrownH, int32 Tiers, float SMin, float SMax, float Wind,
        float MinT, float MaxT, float Draw, bool bFar, std::initializer_list<TPair<B, float>> Weights) -> FFloraSpecies&
    {
        FFloraSpecies& S = L.AddDefaulted_GetRef();
        S.Id = Id; S.Name = Name; S.Form = Form; S.Bark = Bark; S.Leaves = Leaves;
        S.TrunkHeight = TrunkH; S.TrunkWidth = TrunkW; S.CrownWidth = CrownW; S.CrownHeight = CrownH; S.CrownTiers = Tiers;
        S.ScaleMin = SMin; S.ScaleMax = SMax; S.Wind = Wind; S.MinTemperature = MinT; S.MaxTemperature = MaxT;
        S.DrawDistance = Draw; S.bFar = bFar;
        for (const TPair<B, float>& W : Weights)
        {
            S.BiomeWeight[static_cast<int32>(W.Key)] = W.Value;
        }
        return S;
    };
    using F = EFloraForm;
    using S = EAmeSurface;
    Add(EFloraSpecies::Oak, TEXT("Carvalho-de-Elden"), F::Blocky, S::BarkOak, S::LeavesBroad, 3.2f, .8f, 6.f, 4.4f, 3, .8f, 1.25f, .45f, 4.f, 40.f, 2000.f, true,
        { { B::Plains, 1.f }, { B::Meadow, 1.f }, { B::Forest, .55f }, { B::RiverBank, .45f }, { B::Wetland, .2f } });
    Add(EFloraSpecies::Birch, TEXT("Bétula-Clara"), F::Blocky, S::BarkBirch, S::LeavesBirch, 5.2f, .4f, 2.8f, 4.2f, 3, .85f, 1.2f, .7f, -4.f, 30.f, 1600.f, true,
        { { B::Plains, .3f }, { B::Meadow, .3f }, { B::Forest, .25f }, { B::RiverBank, .25f }, { B::Mountain, .15f } });
    Add(EFloraSpecies::Pine, TEXT("Pinheiro-Escalonado"), F::Conical, S::BarkPine, S::LeavesNeedle, 1.6f, .6f, 5.2f, 7.4f, 5, .8f, 1.35f, .3f, -20.f, 22.f, 2000.f, true,
        { { B::Mountain, 1.f }, { B::Forest, .12f }, { B::SnowPeak, .25f }, { B::Plains, .03f } });
    Add(EFloraSpecies::Bush, TEXT("Arbusto-de-Blocos"), F::Blocky, S::BarkOak, S::LeavesBroad, 0.f, 0.f, 2.4f, 1.4f, 3, .7f, 1.3f, .8f, 0.f, 40.f, 450.f, false,
        { { B::Plains, .25f }, { B::Meadow, .3f }, { B::Forest, .1f }, { B::RiverBank, .2f }, { B::Wetland, .3f } });
    Add(EFloraSpecies::Willow, TEXT("Salgueiro-do-Rio"), F::Blocky, S::BarkOak, S::LeavesBirch, 3.4f, .7f, 6.4f, 3.2f, 2, .85f, 1.2f, .9f, 5.f, 35.f, 1600.f, true,
        { { B::RiverBank, .6f }, { B::Wetland, .8f } });
    Add(EFloraSpecies::Palm, TEXT("Palmeira-Angular"), F::Angular, S::BarkPine, S::LeavesBroad, 6.5f, .45f, 6.f, 1.2f, 7, .85f, 1.2f, .85f, 8.f, 45.f, 1600.f, true,
        { { B::Beach, 1.f } });
    Add(EFloraSpecies::Ancient, TEXT("Árvore Anciã"), F::Organic, S::BarkOak, S::LeavesBroad, 5.f, 2.2f, 13.f, 9.f, 9, .9f, 1.15f, .2f, 5.f, 35.f, 4000.f, true,
        { { B::Forest, .003f }, { B::Meadow, .004f }, { B::Plains, .002f } }); // rare: ~1 in 300 forest trees
    return L;
}

// A box with its own axes (half extents along each), for leaning trunks and fronds.
void OrientedBox(FAhgMeshBuilder& Out, const FVector3f& C, const FVector3f& X, const FVector3f& Y, const FVector3f& Z, const FVector3f& Half)
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

void CenteredBox(FAhgMeshBuilder& Out, const FVector3f& Center, const FVector3f& Size)
{
    Out.Box(Center - FVector3f(Size.X, Size.Y, 0) * .5f, Center + FVector3f(Size.X * .5f, Size.Y * .5f, Size.Z));
}
}

const TArray<FFloraSpecies>& FFloraRegistry::All()
{
    static const TArray<FFloraSpecies> Species = MakeSpecies();
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
            || S.TrunkHeight < 0.f || S.CrownTiers < 1 || S.DrawDistance <= 0.f || S.MinTemperature >= S.MaxTemperature)
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
    return Problems;
}

int32 FFloraRegistry::Pick(EAmeBiome Biome, float Temperature, float Roll)
{
    const int32 Index = static_cast<int32>(Biome);
    float Total = 0.f;
    for (const FFloraSpecies& S : All())
    {
        if (Temperature >= S.MinTemperature && Temperature <= S.MaxTemperature)
        {
            Total += S.BiomeWeight[Index];
        }
    }
    if (Total <= 0.f)
    {
        return -1;
    }
    float Left = Roll * Total;
    int32 Last = -1;
    for (const FFloraSpecies& S : All())
    {
        if (Temperature >= S.MinTemperature && Temperature <= S.MaxTemperature && S.BiomeWeight[Index] > 0.f)
        {
            Last = static_cast<int32>(S.Id);
            Left -= S.BiomeWeight[Index];
            if (Left < 0.f)
            {
                return Last;
            }
        }
    }
    return Last;
}

void FFloraRegistry::BuildMesh(const FFloraSpecies& S, int32 Seed, FAhgMeshBuilder& Out)
{
    const float TH = S.TrunkHeight * 100.f, TW = S.TrunkWidth * 100.f, CW = S.CrownWidth * 100.f, CH = S.CrownHeight * 100.f;
    const uint32 Salt = static_cast<uint32>(S.Id) * 101u;
    auto R = [Seed, Salt](int32 K) { return Hash01(Seed, static_cast<int32>(Salt), K, 77u); }; // fixed per species and seed
    const int32 First = Out.Verts.Num();
    switch (S.Form)
    {
    case EFloraForm::Blocky:
    {
        if (TH > 0.f)
        {
            Out.Surface = SurfaceId(S.Bark);
            CenteredBox(Out, FVector3f(0, 0, -30), FVector3f(TW, TW, TH + 60.f));
            // One or two square branches out of the trunk for a less totemic silhouette.
            CenteredBox(Out, FVector3f(TW * .9f, 0, TH * .62f), FVector3f(TW * 1.4f, TW * .45f, TW * .45f));
            if (S.TrunkHeight > 4.f)
            {
                CenteredBox(Out, FVector3f(-TW * .8f, TW * .3f, TH * .78f), FVector3f(TW * 1.2f, TW * .4f, TW * .4f));
            }
        }
        Out.Surface = SurfaceId(S.Leaves);
        if (S.Id == EFloraSpecies::Willow)
        {
            // A wide flat canopy and hanging rectangular curtains along its edges.
            CenteredBox(Out, FVector3f(0, 0, TH), FVector3f(CW, CW, CH * .45f));
            CenteredBox(Out, FVector3f(0, 0, TH + CH * .45f), FVector3f(CW * .6f, CW * .6f, CH * .3f));
            for (int32 K = 0; K < 12; ++K)
            {
                const float A = 2.f * PI * K / 12.f;
                const float Drop = CH * (.7f + .5f * R(K));
                CenteredBox(Out, FVector3f(FMath::Cos(A) * CW * .44f, FMath::Sin(A) * CW * .44f, TH + CH * .3f - Drop), FVector3f(CW * .14f, CW * .14f, Drop));
            }
            break;
        }
        // Stacked boxes that shrink upward, each nudged off-centre, and a few side blocks.
        float Z = FMath::Max(TH - CH * .1f, 0.f);
        for (int32 T = 0; T < S.CrownTiers; ++T)
        {
            const float F = 1.f - .26f * T;
            const float H = CH * (T == 0 ? .42f : .58f / FMath::Max(S.CrownTiers - 1, 1));
            const FVector3f Off((R(T * 3) - .5f) * CW * .12f, (R(T * 3 + 1) - .5f) * CW * .12f, 0.f);
            CenteredBox(Out, FVector3f(0, 0, Z) + Off, FVector3f(CW * F, CW * F * (.85f + .25f * R(T * 3 + 2)), H));
            Z += H;
        }
        const int32 Side = S.TrunkHeight > 0.f ? 3 : 2;
        for (int32 K = 0; K < Side; ++K)
        {
            const float A = 2.f * PI * (K / static_cast<float>(Side) + R(20 + K) * .2f);
            const float Bx = CW * (.35f + .15f * R(30 + K));
            CenteredBox(Out, FVector3f(FMath::Cos(A) * CW * .5f, FMath::Sin(A) * CW * .5f, FMath::Max(TH - CH * .05f, 0.f) + CH * .15f * R(40 + K)), FVector3f(Bx, Bx, CH * .32f));
        }
        break;
    }
    case EFloraForm::Conical:
    {
        Out.Surface = SurfaceId(S.Bark);
        CenteredBox(Out, FVector3f(0, 0, -30), FVector3f(TW, TW, TH + CH * .6f));
        Out.Surface = SurfaceId(S.Leaves);
        // Square pyramids (4-sided cones), overlapping, narrowing to a point: a stepped triangle.
        const float TierH = CH / (S.CrownTiers * .72f + .28f);
        for (int32 T = 0; T < S.CrownTiers; ++T)
        {
            const float F = 1.f - static_cast<float>(T) / (S.CrownTiers + .4f);
            Out.Cylinder(FVector3f(0, 0, TH + T * TierH * .72f), CW * .5f * F * 1.414f, TierH, 4, true, 0.f);
        }
        break;
    }
    case EFloraForm::Angular:
    {
        // A segmented trunk leaning in steps, and flat rectangular fronds bent down from the crown.
        Out.Surface = SurfaceId(S.Bark);
        const int32 Segments = 6;
        FVector3f At(0, 0, -30);
        for (int32 K = 0; K < Segments; ++K)
        {
            const float H = (TH + 30.f) / Segments;
            const float W = TW * (1.15f - .3f * K / Segments);
            CenteredBox(Out, At, FVector3f(W, W, H + 4.f));
            At += FVector3f(H * .14f * (1.f + .5f * K / Segments), 0, H);
        }
        Out.Surface = SurfaceId(S.Leaves);
        CenteredBox(Out, At - FVector3f(0, 0, 20.f), FVector3f(TW * 1.6f, TW * 1.6f, 50.f));
        for (int32 K = 0; K < S.CrownTiers; ++K)
        {
            const float A = 2.f * PI * (K + .3f * R(K)) / S.CrownTiers;
            const FVector3f Out2(FMath::Cos(A), FMath::Sin(A), 0.f);
            const float Len = CW * .5f * (.8f + .3f * R(10 + K));
            const float Bend = -.35f - .25f * R(20 + K);
            const FVector3f Axis = (Out2 + FVector3f(0, 0, Bend)).GetSafeNormal();
            const FVector3f Width = FVector3f::CrossProduct(FVector3f(0, 0, 1), Out2).GetSafeNormal();
            const FVector3f Up = FVector3f::CrossProduct(Axis, Width).GetSafeNormal();
            OrientedBox(Out, At + Axis * (Len * .5f) + FVector3f(0, 0, 10.f), Axis, Width, Up, FVector3f(Len * .5f, CW * .07f, 6.f));
        }
        break;
    }
    case EFloraForm::Organic:
    {
        // Rare and old: a flaring trunk, root swellings and a heavy irregular rounded crown.
        Out.Surface = SurfaceId(S.Bark);
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
        Out.Surface = SurfaceId(S.Leaves);
        for (int32 K = 0; K < S.CrownTiers; ++K)
        {
            const float A = 2.f * PI * (K + R(60 + K)) / S.CrownTiers;
            const float Rad = CW * (.18f + .1f * R(70 + K));
            const float D = K == 0 ? 0.f : CW * (.22f + .14f * R(80 + K));
            Out.Sphere(FVector3f(FMath::Cos(A) * D, FMath::Sin(A) * D, TH + CH * (.35f + .35f * R(90 + K))), FVector3f(Rad, Rad, Rad * .8f), 6, 9, .25f, Seed + 100 + K);
        }
        break;
    }
    }
    // Wind weight: UV0.y is the height fraction of each vertex.
    const float Top = FMath::Max(MeshHeight(S), 1.f);
    for (int32 I = First; I < Out.Verts.Num(); ++I)
    {
        Out.Verts[I].UV = FVector2f(0.f, FMath::Clamp(Out.Verts[I].P.Z / Top, 0.f, 1.f));
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
    const FVector2D P(C.X * Cell + Hash01(Seed, C.X, C.Y, 1) * Cell, C.Y * Cell + Hash01(Seed, C.X, C.Y, 2) * Cell);
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
    case B::Forest: Density = .2f + .7f * S.ForestDensity; break;
    case B::Plains: Density = .035f; break;
    case B::Meadow: Density = .025f; break;
    case B::RiverBank: Density = .14f; break;
    case B::Wetland: Density = .08f; break;
    case B::Beach: Density = .03f; break;
    case B::Mountain: Density = S.Height < 280.f ? .1f + .45f * S.ForestDensity : 0.f; break;
    default: break;
    }
    if (Hash01(Seed, C.X, C.Y, 3) > Density)
    {
        return false;
    }
    const int32 Species = FFloraRegistry::Pick(S.Biome, S.Temperature, Hash01(Seed, C.X, C.Y, 4));
    if (Species < 0)
    {
        return false;
    }
    // Not on steep ground (two nearby samples).
    if (FMath::Abs(Gen.Height(P + FVector2D(1.5, 0)) - S.Height) > 1.2f || FMath::Abs(Gen.Height(P + FVector2D(0, 1.5)) - S.Height) > 1.2f)
    {
        return false;
    }
    const FFloraSpecies& Def = FFloraRegistry::All()[Species];
    Out.Species = Def.Id;
    Out.Id = MakeId(C, Def.Id);
    Out.Position = P;
    Out.Ground = S.Height;
    Out.Scale = FMath::Lerp(Def.ScaleMin, Def.ScaleMax, Hash01(Seed, C.X, C.Y, 5));
    Out.Yaw = 360.f * Hash01(Seed, C.X, C.Y, 6);
    Out.Tint = Hash01(Seed, C.X, C.Y, 7) - .5f;
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
