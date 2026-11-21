#if WITH_DEV_AUTOMATION_TESTS

#include "AethraVoxelWorld.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/AethraTerrain.h"
#include "VisualWorld/AethraCaveGen.h"
#include "VisualWorld/AethraFlora.h"
#include "VisualWorld/AethraRuinGen.h"
#include "VisualWorld/AethraHybridGeometry.h"
#include "VisualWorld/AethraVisualWorld.h"
#include "VisualWorld/AethraWorldGen.h"

// Aethra.VisualWorld.*: the Visual World prototype (AME, WorldGen, HydroGen, AHG, flora). They run in
// their own temporary folder (Saved/VisualWorldTests) and delete it; the voxel saves are not touched.

#define AETHRA_VW_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
constexpr int32 VWSeed = AAethraVisualWorld::DemoSeed;

FString TestDir()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VisualWorldTests"));
}

void CleanTestDir()
{
    IFileManager::Get().DeleteDirectory(*TestDir(), false, true);
}

// The generator is the expensive part (~0.2 s): one per seed for the whole run.
const FAethraWorldGen& Gen(int32 S = VWSeed)
{
    static TMap<int32, TUniquePtr<FAethraWorldGen>> Cache;
    TUniquePtr<FAethraWorldGen>& G = Cache.FindOrAdd(S);
    if (!G)
    {
        G = MakeUnique<FAethraWorldGen>(S);
    }
    return *G;
}

template <typename FBody>
void WithWorld(FBody&& Body)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    Body(*World);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
}

AAethraVisualWorld* StartVisualWorld(UWorld& World, const FString& PiecesFile, int32 S = VWSeed)
{
    AAethraVisualWorld* VW = World.SpawnActor<AAethraVisualWorld>();
    VW->Start(S, FVector::ZeroVector, PiecesFile);
    return VW;
}

// Terrain vertices of a chunk keyed by their (x, y) in centimetres: the highest z at each (skirts hang below).
TMap<FIntPoint, double> Surface(const UProceduralMeshComponent* Mesh)
{
    TMap<FIntPoint, double> Out;
    if (!Mesh)
    {
        return Out;
    }
    const FProcMeshSection* Section = const_cast<UProceduralMeshComponent*>(Mesh)->GetProcMeshSection(0);
    for (const FProcMeshVertex& V : Section->ProcVertexBuffer)
    {
        const FIntPoint Key(FMath::RoundToInt(V.Position.X), FMath::RoundToInt(V.Position.Y));
        double& Z = Out.FindOrAdd(Key, -1e12);
        Z = FMath::Max(Z, V.Position.Z);
    }
    return Out;
}

uint32 HashSurface(const TMap<FIntPoint, double>& S)
{
    TArray<FIntPoint> Keys;
    S.GetKeys(Keys);
    Keys.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.Y != B.Y ? A.Y < B.Y : A.X < B.X; });
    uint32 H = 0;
    for (const FIntPoint& K : Keys)
    {
        H = HashCombine(H, HashCombine(GetTypeHash(K), GetTypeHash(FMath::RoundToInt(S[K] * 10.0))));
    }
    return H;
}

FAmeEnvironment RandomEnvironment(FRandomStream& R)
{
    FAmeEnvironment E;
    E.Biome = static_cast<EAmeBiome>(R.RandRange(0, static_cast<int32>(EAmeBiome::Count) - 1));
    E.Rock = static_cast<EAmeRock>(R.RandRange(0, static_cast<int32>(EAmeRock::Count) - 1));
    E.Altitude = R.FRandRange(-5.f, 900.f);
    E.Moisture = R.FRand();
    E.Temperature = R.FRandRange(-15.f, 30.f);
    E.Slope = R.FRand();
    E.WaterDistance = R.FRandRange(0.f, 200.f);
    return E;
}
}

// ---------------------------------------------------------------- 01 deterministic material selection

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW01, "Aethra.VisualWorld.01.MaterialSelectionDeterministic", AETHRA_VW_FLAGS)
bool FAethraVW01::RunTest(const FString&)
{
    FRandomStream R(7);
    TArray<FAmeEnvironment> Envs;
    TArray<FIntPoint> Cells;
    TArray<FAmeChoice> First;
    for (int32 I = 0; I < 4000; ++I)
    {
        Envs.Add(RandomEnvironment(R));
        Cells.Add(FIntPoint(R.RandRange(-100000, 100000), R.RandRange(-100000, 100000)));
        First.Add(FAmeSelector::Terrain(Envs.Last(), 4242, Cells.Last()));
    }
    // Same answers in the opposite order (no hidden state).
    int32 Mismatch = 0;
    for (int32 I = Envs.Num() - 1; I >= 0; --I)
    {
        const FAmeChoice C = FAmeSelector::Terrain(Envs[I], 4242, Cells[I]);
        Mismatch += C.Primary != First[I].Primary || C.Secondary != First[I].Secondary || C.Blend != First[I].Blend;
    }
    TestEqual(TEXT("Same environment, seed and cell give the same surface in any order"), Mismatch, 0);

    // The environment decides: steep ground shows the local rock, cold ground snow, the water's edge sand/gravel/mud.
    FAmeEnvironment Steep;
    Steep.Biome = EAmeBiome::Plains;
    Steep.Rock = EAmeRock::Basalt;
    Steep.Slope = .8f;
    const FAmeChoice SteepChoice = FAmeSelector::Terrain(Steep, 1, FIntPoint(3, 4));
    TestTrue(TEXT("Steep ground blends fully into its rock"), SteepChoice.Secondary == EAmeSurface::Basalt && SteepChoice.Blend >= .99f);
    FAmeEnvironment Cold;
    Cold.Temperature = -8.f;
    Cold.Slope = .1f;
    TestEqual(TEXT("Cold gentle ground is snow"), FAmeSelector::Terrain(Cold, 1, FIntPoint(0, 0)).Primary, EAmeSurface::Snow);
    FAmeEnvironment Bank;
    Bank.Biome = EAmeBiome::Forest;
    Bank.WaterDistance = 1.f;
    const EAmeSurface Edge = FAmeSelector::Terrain(Bank, 1, FIntPoint(0, 0)).Primary;
    TestTrue(TEXT("The water's edge is sand, gravel or mud"), Edge == EAmeSurface::Sand || Edge == EAmeSurface::Gravel || Edge == EAmeSurface::Mud);
    // Weather and wear never change the surface.
    FAmeEnvironment Wet = Steep;
    Wet.Wetness = 1.f;
    Wet.Wear = 1.f;
    const FAmeChoice WetChoice = FAmeSelector::Terrain(Wet, 1, FIntPoint(3, 4));
    TestTrue(TEXT("Wetness and wear do not change the choice"), WetChoice.Primary == SteepChoice.Primary && WetChoice.Secondary == SteepChoice.Secondary);
    // Buildings: culture and wealth decide; the local rock shows up where the culture uses it.
    TestEqual(TEXT("Elden roof, modest"), FAmeSelector::Building(EAmeCulture::EldenStone, EAmePart::Roof, EAmeRock::Granite, .2f), EAmeSurface::RoofTile);
    TestEqual(TEXT("Elden roof, rich"), FAmeSelector::Building(EAmeCulture::EldenStone, EAmePart::Roof, EAmeRock::Granite, .9f), EAmeSurface::Ceramic);
    TestEqual(TEXT("Highland wall uses the local rock"), FAmeSelector::Building(EAmeCulture::HighlandSlate, EAmePart::Wall, EAmeRock::Limestone, .5f), EAmeSurface::Limestone);
    return true;
}

// ---------------------------------------------------------------- 02 biome rules

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW02, "Aethra.VisualWorld.02.BiomeRules", AETHRA_VW_FLAGS)
bool FAethraVW02::RunTest(const FString&)
{
    const FAethraWorldGen& G = Gen();
    TMap<EAmeBiome, int32> Seen;
    int32 Broken = 0, Pairs = 0, Changes = 0;
    // A 4 x 4 km window, 32 m apart, and each point against a neighbour 8 m east.
    for (int32 Y = -64; Y < 64; ++Y)
    {
        for (int32 X = -64; X < 64; ++X)
        {
            const FVector2D P(X * 32.0, Y * 32.0);
            const FWorldSample S = G.Sample(P);
            Seen.FindOrAdd(S.Biome)++;
            if ((S.Biome == EAmeBiome::SnowPeak && S.Temperature >= -1.f)
                || (S.Biome == EAmeBiome::Mountain && S.Height <= 160.f)
                || (S.Biome == EAmeBiome::RiverBank && S.RiverStrength <= 0.f)
                || (S.Biome == EAmeBiome::Beach && S.Height >= 2.5f)
                || S.Biome >= EAmeBiome::Desert) // not produced by the prototype
            {
                ++Broken;
            }
            ++Pairs;
            Changes += G.Sample(P + FVector2D(8.0, 0.0)).Biome != S.Biome;
        }
    }
    TestEqual(TEXT("Every sample follows the biome rules"), Broken, 0);
    const float ChangeRate = static_cast<float>(Changes) / Pairs;
    AddInfo(FString::Printf(TEXT("%d biomes in the window; biome changes between points 8 m apart: %.1f%%"), Seen.Num(), ChangeRate * 100.f));
    TestTrue(TEXT("Varied landscape: at least 5 biomes in 4 x 4 km"), Seen.Num() >= 5);
    TestTrue(TEXT("Regions, not a mosaic: under 12% of 8 m steps change biome"), ChangeRate < .12f);
    return true;
}

// ---------------------------------------------------------------- 03 chunk consistency

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW03, "Aethra.VisualWorld.03.ChunkConsistency", AETHRA_VW_FLAGS)
bool FAethraVW03::RunTest(const FString&)
{
    CleanTestDir();
    // Same seed, two generators: same samples (nothing depends on evaluation order or history).
    const FAethraWorldGen& A = Gen();
    FAethraWorldGen B(VWSeed);
    int32 Diff = 0;
    FRandomStream R(3);
    for (int32 I = 0; I < 3000; ++I)
    {
        const FVector2D P(R.FRandRange(-3000.f, 3000.f), R.FRandRange(-3000.f, 3000.f));
        const FWorldSample SA = A.Sample(P), SB = B.Sample(P);
        Diff += SA.Height != SB.Height || SA.WaterLevel != SB.WaterLevel || SA.Biome != SB.Biome || SA.WaterBody != SB.WaterBody;
    }
    TestEqual(TEXT("Two generators with the same seed agree everywhere"), Diff, 0);

    // Neighbouring chunks share their border vertices exactly (no cracks), in both directions.
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        int32 Borders = 0, Mismatches = 0, Shared = 0;
        for (int32 Y = -2; Y < 2; ++Y)
        {
            for (int32 X = -2; X < 2; ++X)
            {
                const TMap<FIntPoint, double> Here = Surface(VW->ChunkMesh(0, FIntPoint(X, Y)));
                for (const FIntPoint& Next : { FIntPoint(X + 1, Y), FIntPoint(X, Y + 1) })
                {
                    const TMap<FIntPoint, double> There = Surface(VW->ChunkMesh(0, Next));
                    if (!Here.Num() || !There.Num())
                    {
                        continue;
                    }
                    ++Borders;
                    for (const TPair<FIntPoint, double>& V : Here)
                    {
                        if (const double* Z = There.Find(V.Key))
                        {
                            ++Shared;
                            Mismatches += FMath::Abs(*Z - V.Value) > .01;
                        }
                    }
                }
            }
        }
        AddInfo(FString::Printf(TEXT("%d borders, %d shared vertices"), Borders, Shared));
        TestTrue(TEXT("Borders were compared"), Borders >= 8 && Shared > 500);
        TestEqual(TEXT("Shared border vertices have the same height"), Mismatches, 0);
    });
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 04 geometry persistence

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW04, "Aethra.VisualWorld.04.GeometryPersistence", AETHRA_VW_FLAGS)
bool FAethraVW04::RunTest(const FString&)
{
    CleanTestDir();
    const FString Path = FPaths::Combine(TestDir(), TEXT("Layer.ahg"));
    IFileManager::Get().MakeDirectory(*TestDir(), true);
    FAhgLayer Layer;
    TArray<int32> Ids;
    for (int32 S = 0; S < static_cast<int32>(EAhgShape::Count); ++S)
    {
        FAhgPiece P;
        P.Shape = static_cast<EAhgShape>(S);
        P.Surface = static_cast<EAmeSurface>(S % static_cast<int32>(EAmeSurface::Count));
        P.Location = FVector(S * 250.0, -120.0, 37.5);
        P.Yaw = S * 15.f;
        P.Size = FVector(1.f + S * .1f, 2.f, .5f + S * .25f);
        Ids.Add(Layer.Add(P));
    }
    TestFalse(TEXT("Every shape placed"), Ids.Contains(0));
    TestTrue(TEXT("Saved"), Layer.SaveToFile(Path));

    FAhgLayer Loaded;
    TestTrue(TEXT("Loaded"), Loaded.LoadFromFile(Path));
    TestEqual(TEXT("Same count"), Loaded.GetPieces().Num(), Layer.GetPieces().Num());
    bool bSame = Loaded.GetPieces().Num() == Layer.GetPieces().Num();
    for (int32 I = 0; bSame && I < Layer.GetPieces().Num(); ++I)
    {
        bSame = Loaded.GetPieces()[I] == Layer.GetPieces()[I];
    }
    TestTrue(TEXT("Every piece identical after a reload"), bSame);

    TestTrue(TEXT("Removed"), Loaded.Remove(Ids[3]));
    TestTrue(TEXT("Saved after removal"), Loaded.SaveToFile(Path));
    FAhgLayer Again;
    TestTrue(TEXT("Reloaded"), Again.LoadFromFile(Path));
    TestNull(TEXT("The removed piece stays removed"), Again.Find(Ids[3]));
    TestEqual(TEXT("The others stay"), Again.GetPieces().Num(), Ids.Num() - 1);
    const int32 NewId = Again.Add(FAhgPiece());
    TestFalse(TEXT("Ids are not reused after a reload"), Ids.Contains(NewId));

    // A damaged file is refused and leaves the layer as it was.
    TArray<uint8> Bytes;
    FFileHelper::LoadFileToArray(Bytes, *Path);
    Bytes.SetNum(Bytes.Num() - 7);
    FFileHelper::SaveArrayToFile(Bytes, *Path);
    TestFalse(TEXT("Truncated file refused"), Again.LoadFromFile(Path));
    TestEqual(TEXT("Layer unchanged by the failed load"), Again.GetPieces().Num(), Ids.Num());

    // The settlement of the Visual World is saved on first start and loaded, not rebuilt, afterwards.
    const FString Pieces = FPaths::Combine(TestDir(), TEXT("Pieces.ahg"));
    int32 FirstCount = 0;
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        FirstCount = VW->Pieces().GetPieces().Num();
        TestTrue(TEXT("Settlement written"), IFileManager::Get().FileExists(*Pieces));
        FAhgPiece Extra;
        Extra.Location = FVector(VW->SettlementCenter * 100.0, 0.0);
        TestTrue(TEXT("Player piece added"), VW->Pieces().Add(Extra) > 0);
        TestTrue(TEXT("Saved"), VW->SavePieces());
    });
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        TestEqual(TEXT("Restart loads the settlement and the added piece"), VW->Pieces().GetPieces().Num(), FirstCount + 1);
    });
    AddInfo(FString::Printf(TEXT("Settlement: %d pieces"), FirstCount));
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 05 existing saves preserved

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW05, "Aethra.VisualWorld.05.ExistingSavesPreserved", AETHRA_VW_FLAGS)
bool FAethraVW05::RunTest(const FString&)
{
    // The voxel generator is untouched: a checksum of 4096 columns of seed 42819, recorded on the
    // base branch (claude/voxel-contract-lis-01). Any change to existing voxel worlds breaks it.
    uint32 Checksum = 0;
    TStaticArray<EAethraBlock, AethraTerrain::Height> Column;
    for (int32 Y = -32; Y < 32; ++Y)
    {
        for (int32 X = -32; X < 32; ++X)
        {
            AethraTerrain::GenerateColumn(FIntPoint(X * 7, Y * 5), 42819, Column);
            for (int32 Z = 0; Z < AethraTerrain::Height; ++Z)
            {
                Checksum = HashCombine(Checksum, static_cast<uint32>(Column[Z]) + Z * 131u);
            }
        }
    }
    AddInfo(FString::Printf(TEXT("Voxel column checksum: 0x%08x"), Checksum));
    TestEqual(TEXT("Voxel terrain generation unchanged"), Checksum, 0xdbc636bfu);

    // Starting the Visual World writes nothing to the voxel save slots.
    const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
    auto Snapshot = [&SaveDir]()
    {
        TMap<FString, FDateTime> Files;
        TArray<FString> Names;
        IFileManager::Get().FindFiles(Names, *FPaths::Combine(SaveDir, TEXT("*.sav")), true, false);
        for (const FString& N : Names)
        {
            Files.Add(N, IFileManager::Get().GetTimeStamp(*FPaths::Combine(SaveDir, N)));
        }
        return Files;
    };
    const TMap<FString, FDateTime> Before = Snapshot();
    CleanTestDir();
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->UpdateStreaming(FVector(50000.0, 0.0, 0.0), 200);
        VW->SavePieces();
    });
    const TMap<FString, FDateTime> After = Snapshot();
    TestTrue(TEXT("No voxel save created, changed or deleted"), Before.OrderIndependentCompareEqual(After));
    TestFalse(TEXT("No voxel slot for the Visual World seed"), UGameplayStatics::DoesSaveGameExist(FString::Printf(TEXT("AethraWorld_%d_0"), VWSeed), 0) && !Before.Contains(FString::Printf(TEXT("AethraWorld_%d_0.sav"), VWSeed)));
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 06 river continuity

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW06, "Aethra.VisualWorld.06.RiverContinuity", AETHRA_VW_FLAGS)
bool FAethraVW06::RunTest(const FString&)
{
    const FAethraWorldGen& G = Gen();
    const TArray<FIntPoint>& River = G.MainRiver;
    TestTrue(TEXT("A main river exists and is long (> 3 km of nodes)"), River.Num() > 3000 / 16);
    int32 Breaks = 0, Uphill = 0, Dry = 0, Checked = 0;
    for (int32 I = 0; I + 1 < River.Num(); ++I)
    {
        const FIntPoint A = River[I], B = River[I + 1];
        // Upstream to downstream: every node drains into the next one.
        Breaks += G.NodeDownstream(A) != B.Y * FAethraWorldGen::Nodes + B.X;
        Uphill += G.NodeLevel(B) > G.NodeLevel(A) + .01f;
        // The drawn river (through the warp) holds water at each node.
        const FVector2D P = G.WorldOfGridPoint(G.NodePosition(A));
        if (P.Size() < 3500.0)
        {
            ++Checked;
            bool bWater = false;
            for (int32 K = 0; K < 9 && !bWater; ++K)
            {
                bWater = G.Sample(P + FVector2D(K % 3 - 1, K / 3 - 1) * 2.0).IsWater();
            }
            Dry += !bWater;
        }
    }
    AddInfo(FString::Printf(TEXT("Main river: %d nodes (%.1f km); %d checked in the world, %d dry"), River.Num(), River.Num() * 16 / 1000.f, Checked, Dry));
    TestEqual(TEXT("No break along the course"), Breaks, 0);
    TestEqual(TEXT("Never flows uphill"), Uphill, 0);
    TestTrue(TEXT("Water along at least 95% of the drawn course"), Checked > 50 && Dry <= Checked / 20);
    // Across a chunk border (128 m) the water surface is continuous: same level from both sides.
    int32 Jumps = 0, Crossings = 0;
    for (const FIntPoint& N : River)
    {
        const FVector2D P = G.WorldOfGridPoint(G.NodePosition(N));
        const double BorderX = FMath::RoundToDouble(P.X / 128.0) * 128.0;
        if (FMath::Abs(P.X - BorderX) < 8.0 && P.Size() < 3500.0)
        {
            const FWorldSample L = G.Sample(FVector2D(BorderX - .01, P.Y)), R = G.Sample(FVector2D(BorderX + .01, P.Y));
            if (L.IsWater() && R.IsWater())
            {
                ++Crossings;
                Jumps += FMath::Abs(L.WaterLevel - R.WaterLevel) > .05f;
            }
        }
    }
    TestEqual(TEXT("No water-level jump at chunk borders"), Jumps, 0);
    AddInfo(FString::Printf(TEXT("%d border crossings checked"), Crossings));
    return true;
}

// ---------------------------------------------------------------- 07 fresh versus salt water

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW07, "Aethra.VisualWorld.07.FreshVersusSaltWater", AETHRA_VW_FLAGS)
bool FAethraVW07::RunTest(const FString&)
{
    const TArray<FHydroBody>& Bodies = Gen().GetBodies();
    const FHydroBody* Sea = Bodies.FindByPredicate([](const FHydroBody& B) { return B.Kind == EHydroKind::Sea; });
    TestNotNull(TEXT("The world has a sea"), Sea);
    if (!Sea)
    {
        return false;
    }
    TestTrue(TEXT("The sea is salt water (>= 30 g/kg)"), Sea->IsSalty() && Sea->Props.Salinity >= 30.f);
    for (const FHydroBody& B : Bodies)
    {
        if (B.Kind == EHydroKind::River || B.Kind == EHydroKind::Stream || B.Kind == EHydroKind::Lake || B.Kind == EHydroKind::Waterfall)
        {
            TestFalse(*FString::Printf(TEXT("%s is fresh water"), *B.Name), B.IsSalty());
            TestTrue(*FString::Printf(TEXT("%s is lighter than the sea"), *B.Name), B.Density() < Sea->Density());
        }
    }
    // Buoyancy is prepared: a 1010 kg/m3 body sinks in fresh water and floats in the sea.
    FHydroBody Fresh;
    Fresh.Props = FHydroGen::Preset(TEXT("ClearLake"));
    TestEqual(TEXT("Sinks in fresh water"), Fresh.FloatFraction(1010.f), 0.f);
    TestTrue(TEXT("Floats in sea water"), Sea->FloatFraction(1010.f) > 0.f);
    FHydroBody Salt;
    Salt.Props = FHydroGen::Preset(TEXT("SaltLake"));
    TestTrue(TEXT("A salt lake is denser than the sea"), Salt.Density() > Sea->Density());
    // Salinity never sets the colour: same water with and without salt has the same optics.
    FHydroProperties P = FHydroGen::Preset(TEXT("GreenRiver"));
    const FHydroOptics O1 = FHydroGen::ComputeOptics(P);
    P.Salinity = 35.f;
    const FHydroOptics O2 = FHydroGen::ComputeOptics(P);
    TestTrue(TEXT("Salinity does not change the colour"), O1.InScatter.Equals(O2.InScatter) && O1.Absorption.Equals(O2.Absorption));
    // ...while what is in the water does: silt is browner (more red than blue) than clear water.
    const FHydroOptics Silt = FHydroGen::ComputeOptics(FHydroGen::Preset(TEXT("SiltRiver")));
    const FHydroOptics Clear = FHydroGen::ComputeOptics(FHydroGen::Preset(TEXT("ClearLake")));
    TestTrue(TEXT("Silty water is warmer in hue than clear water"), Silt.InScatter.R / Silt.InScatter.B > Clear.InScatter.R / Clear.InScatter.B);
    TestTrue(TEXT("At least three distinct water profiles in this world"), Bodies.Num() >= 3);
    return true;
}

// ---------------------------------------------------------------- 08 depth variation

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW08, "Aethra.VisualWorld.08.DepthVariation", AETHRA_VW_FLAGS)
bool FAethraVW08::RunTest(const FString&)
{
    // Optics: deeper water transmits less of the bottom and tends to the water's own colour.
    const FHydroOptics O = FHydroGen::ComputeOptics(FHydroGen::Preset(TEXT("GreenRiver")));
    const FLinearColor Sand(.6f, .55f, .4f);
    float Last = 2.f;
    bool bMonotonic = true;
    for (float D = 0.f; D <= 20.f; D += .5f)
    {
        const float T = FHydroGen::Transmittance(O, D);
        bMonotonic &= T <= Last;
        Last = T;
    }
    TestTrue(TEXT("Transmittance falls with depth"), bMonotonic);
    TestTrue(TEXT("Shallow water shows the bottom"), FHydroGen::ApparentColor(O, .05f, Sand).Equals(Sand, .05f));
    TestTrue(TEXT("Deep water shows its in-scattered colour"), FHydroGen::ApparentColor(O, 60.f, Sand).Equals(O.InScatter, .01f));

    // World: depth varies along the river and grows downstream with the flow.
    const FAethraWorldGen& G = Gen();
    TArray<float> Depths;
    for (const FIntPoint& N : G.MainRiver)
    {
        const FWorldSample S = G.Sample(G.WorldOfGridPoint(G.NodePosition(N)));
        // River water only: lakes on the course and the sea at the mouth have their own depths.
        if (S.IsWater() && G.GetBodies().IsValidIndex(S.WaterBody) && G.GetBodies()[S.WaterBody].Kind == EHydroKind::River)
        {
            Depths.Add(S.WaterLevel - S.Height);
        }
    }
    TestTrue(TEXT("River samples found"), Depths.Num() > 50);
    if (Depths.Num() > 50)
    {
        const float Min = FMath::Min(Depths), Max = FMath::Max(Depths);
        double Up = 0, Down = 0;
        const int32 Half = Depths.Num() / 2;
        for (int32 I = 0; I < Depths.Num(); ++I)
        {
            (I < Half ? Up : Down) += Depths[I];
        }
        Up /= Half;
        Down /= Depths.Num() - Half;
        AddInfo(FString::Printf(TEXT("River depth %.2f..%.2f m; upstream mean %.2f, downstream mean %.2f"), Min, Max, Up, Down));
        TestTrue(TEXT("Depth varies by more than 1 m"), Max - Min > 1.f);
        TestTrue(TEXT("Deeper downstream"), Down > Up);
    }
    return true;
}

// ---------------------------------------------------------------- 09 registry integrity

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW09, "Aethra.VisualWorld.09.RegistryIntegrity", AETHRA_VW_FLAGS)
bool FAethraVW09::RunTest(const FString&)
{
    for (const FString& Problem : FAmeRegistry::Validate())
    {
        AddError(TEXT("AME: ") + Problem);
    }
    TestEqual(TEXT("One surface per id"), FAmeRegistry::All().Num(), static_cast<int32>(EAmeSurface::Count));
    for (const FAmeMaterialDef& D : FAmeRegistry::All())
    {
        const FAmeMaterialDef* Found = FAmeRegistry::FindByName(D.Name);
        TestTrue(*FString::Printf(TEXT("%s found by name"), D.Name), Found && Found->Id == D.Id);
    }
    UTexture2D* Palette = FAmeRegistry::CreatePalette(GetTransientPackage());
    TestTrue(TEXT("Palette texture 64 x 4"), Palette && Palette->GetSizeX() == FAmeRegistry::PaletteWidth && Palette->GetSizeY() == FAmeRegistry::PaletteRows);

    const TArray<FHydroBody>& Bodies = Gen().GetBodies();
    TestTrue(TEXT("Water bodies fit the water palette"), Bodies.Num() <= FHydroGen::MaxBodies);
    for (int32 I = 0; I < Bodies.Num(); ++I)
    {
        TestEqual(TEXT("Body ids are their index"), Bodies[I].Id, I);
    }
    for (int32 S = 0; S < static_cast<int32>(EAhgShape::Count); ++S)
    {
        const EAhgShape Shape = static_cast<EAhgShape>(S);
        TestTrue(*FString::Printf(TEXT("Shape %d has a name and triangles"), S), *FAhgShapes::Name(Shape) && FAhgShapes::Triangles(Shape) > 0);
    }
    for (const FString& Problem : FFloraRegistry::Validate())
    {
        AddError(TEXT("Flora: ") + Problem);
    }
    return true;
}

// ---------------------------------------------------------------- 10 voxel compatibility

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW10, "Aethra.VisualWorld.10.VoxelCompatibility", AETHRA_VW_FLAGS)
bool FAethraVW10::RunTest(const FString&)
{
    constexpr int32 VoxelSeed = 990431;
    auto DeleteSlots = []()
    {
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            const FString Name = FString::Printf(TEXT("AethraWorld_%d_%d"), VoxelSeed, Slot);
            if (UGameplayStatics::DoesSaveGameExist(Name, 0))
            {
                UGameplayStatics::DeleteGameInSlot(Name, 0);
            }
        }
    };
    DeleteSlots();
    WithWorld([&](UWorld& World)
    {
        FActorSpawnParameters Params;
        Params.bDeferConstruction = true;
        AAethraVoxelWorld* Voxels = World.SpawnActor<AAethraVoxelWorld>(Params);
        Voxels->Seed = VoxelSeed;
        Voxels->FinishSpawning(FTransform::Identity);
        Voxels->PrepareSpawn();

        // A dry column (above the water line) with air over its surface block.
        FIntPoint Column(0, 0);
        int32 Surface = 0;
        for (int32 X = 0; X < 400; ++X)
        {
            Column = FIntPoint(X % 20, X / 20);
            Surface = AethraTerrain::SurfaceAt(Column, VoxelSeed);
            if (Surface > AethraTerrain::WaterLevel + 1 && Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface + 1)) == EAethraBlock::Air
                && Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface)) != EAethraBlock::Air)
            {
                break;
            }
        }
        const double Cell = AethraTerrain::CellSizeCm;
        const FVector Base((Column.X + .5) * Cell, (Column.Y + .5) * Cell, (Surface + 1) * Cell);
        // A 1 m piece in the air right above the ground.
        FAhgLayer Layer;
        FAhgPiece Piece;
        Piece.Shape = EAhgShape::Arch;
        Piece.Location = Base;
        const EAethraBlock Above = Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface + 1));
        const EAethraBlock Ground = Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface));
        FString Why;
        const int32 Id = Layer.Add(Piece, Voxels, &Why);
        TestTrue(TEXT("A piece fits in an air cell above the ground"), Id > 0);
        TestFalse(TEXT("Same cell twice is refused"), Layer.Add(Piece, Voxels, &Why) > 0);
        AddInfo(TEXT("Second piece: ") + Why);
        FAhgPiece Buried = Piece;
        Buried.Location.Z -= Cell; // the ground cell
        TestFalse(TEXT("A piece in a solid block is refused"), Layer.Add(Buried, Voxels, &Why) > 0);
        AddInfo(TEXT("Buried piece: ") + Why);
        // Pieces never write voxels: the blocks are as generated.
        TestEqual(TEXT("Air above unchanged"), Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface + 1)), Above);
        TestEqual(TEXT("Ground unchanged"), Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface)), Ground);
        // Removing frees the cell; mining the ground then frees it for a piece.
        TestTrue(TEXT("Removed"), Layer.Remove(Id));
        TestTrue(TEXT("The freed cell accepts a piece again"), Layer.Add(Piece, Voxels) > 0);
        TestTrue(TEXT("Mining the ground block"), Voxels->SetBlock(FIntVector(Column.X, Column.Y, Surface), EAethraBlock::Air));
        TestTrue(TEXT("A piece fits where the block was mined"), Layer.Add(Buried, Voxels, &Why) > 0);
        // The voxel world does not know pieces; the layer reports their cells so placing a block there can be refused.
        TestTrue(TEXT("The layer reports its cells as taken"), Layer.IsCellTaken(FIntVector(Column.X, Column.Y, Surface)));
    });
    DeleteSlots();
    return true;
}

// ---------------------------------------------------------------- 11 region streaming

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW11, "Aethra.VisualWorld.11.RegionStreaming", AETHRA_VW_FLAGS)
bool FAethraVW11::RunTest(const FString&)
{
    CleanTestDir();
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        const FIntPoint Home(0, 0);
        TestTrue(TEXT("Chunk at the origin loaded"), VW->HasChunk(0, Home));
        const uint32 HomeHash = HashSurface(Surface(VW->ChunkMesh(0, Home)));
        const int32 NearCount = VW->ChunkCount(0), FarCount = VW->ChunkCount(1);
        AddInfo(FString::Printf(TEXT("At the origin: %d near chunks, %d far chunks"), NearCount, FarCount));
        TestTrue(TEXT("Both levels of detail present"), NearCount > 0 && FarCount > 0);

        // Fly 3 km east: the origin unloads, the new region loads at full detail.
        const FVector Away(300000.0, 0.0, 0.0);
        VW->UpdateStreaming(Away, 1000);
        TestFalse(TEXT("Origin chunk unloaded far away"), VW->HasChunk(0, Home));
        TestTrue(TEXT("Chunk under the viewer loaded"), VW->HasChunk(0, FIntPoint(FMath::FloorToInt(3000.0 / 128.0), 0)));
        TestTrue(TEXT("Chunk counts stay bounded"), VW->ChunkCount(0) <= NearCount + 8 && VW->ChunkCount(1) <= FarCount + 16);

        // Back home: the region regenerates identically.
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        TestTrue(TEXT("Origin chunk reloaded"), VW->HasChunk(0, Home));
        TestEqual(TEXT("Regenerated chunk identical"), HashSurface(Surface(VW->ChunkMesh(0, Home))), HomeHash);
        AddInfo(FString::Printf(TEXT("Generated %d, removed %d chunks"), VW->Stats().Generated, VW->Stats().Removed));
    });
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 12 performance

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW12, "Aethra.VisualWorld.12.Performance", AETHRA_VW_FLAGS)
bool FAethraVW12::RunTest(const FString&)
{
    // Loose limits: they catch serious regressions, not small ones (the frame-rate benchmark is separate).
    double Begin = FPlatformTime::Seconds();
    FAethraWorldGen G(VWSeed + 1);
    const double InitMs = (FPlatformTime::Seconds() - Begin) * 1000.0;
    FRandomStream R(11);
    constexpr int32 Samples = 200000;
    double Sink = 0;
    Begin = FPlatformTime::Seconds();
    for (int32 I = 0; I < Samples; ++I)
    {
        Sink += G.Sample(FVector2D(R.FRandRange(-3000.f, 3000.f), R.FRandRange(-3000.f, 3000.f))).Height;
    }
    const double SampleUs = (FPlatformTime::Seconds() - Begin) * 1e6 / Samples;
    AddInfo(FString::Printf(TEXT("Generator setup %.0f ms; Sample %.2f us (checksum %.0f)"), InitMs, SampleUs, Sink));
    TestTrue(TEXT("Generator setup under 2 s"), InitMs < 2000.0);
    TestTrue(TEXT("A sample under 25 us"), SampleUs < 25.0);

    CleanTestDir();
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        const FVwStats& S = VW->Stats();
        const double MeanChunk = S.Generated ? S.TotalChunkMs / S.Generated : 0.0;
        AddInfo(FString::Printf(TEXT("Start %.0f ms; %d chunks, mean %.1f ms, max %.1f ms; %d trees, %d grass, %d triangles"),
            S.SetupMs, S.Generated, MeanChunk, S.MaxChunkMs, S.Trees, S.Grass, S.Triangles));
        TestTrue(TEXT("Start under 5 s"), S.SetupMs < 5000.0);
        TestTrue(TEXT("Mean chunk build under 60 ms (streaming builds 2 per 0.2 s)"), MeanChunk < 60.0);
    });
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 13-14 flora (vegetation addendum)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW13, "Aethra.VisualWorld.13.FloraSpecies", AETHRA_VW_FLAGS)
bool FAethraVW13::RunTest(const FString&)
{
    for (const FString& Problem : FFloraRegistry::Validate())
    {
        AddError(Problem);
    }
    // Geometric identity: the canopy of common broadleaf species is axis-aligned boxes only (branches
    // may be angular), pines narrow to a point, only rare species are organic. Variants differ.
    for (const FFloraSpecies& S : FFloraRegistry::All())
    {
      TSet<int32> VertexCounts;
      for (int32 Variant = 0; Variant < FFloraRegistry::Variants; ++Variant)
      {
        FAhgMeshBuilder B;
        FFloraRegistry::BuildMesh(S, VWSeed, Variant, B);
        TestTrue(*FString::Printf(TEXT("%s has geometry"), S.Name), B.Verts.Num() > 0);
        int32 Shape = B.Verts.Num();
        for (const FAhgMeshBuilder::FVert& V : B.Verts)
        {
            Shape = HashCombine(Shape, GetTypeHash(FMath::RoundToInt(V.P.X + V.P.Y * 3.f + V.P.Z * 7.f)));
        }
        VertexCounts.Add(Shape);
        if (S.Form == EFloraForm::Blocky)
        {
            int32 Slanted = 0;
            for (const FAhgMeshBuilder::FVert& V : B.Verts)
            {
                if (FMath::RoundToInt(V.Surface) == static_cast<int32>(S.Leaves))
                {
                    Slanted += FMath::Max3(FMath::Abs(V.N.X), FMath::Abs(V.N.Y), FMath::Abs(V.N.Z)) < .999f;
                }
            }
            TestEqual(*FString::Printf(TEXT("%s: the canopy is built from cubes only"), S.Name), Slanted, 0);
        }
        if (S.Form == EFloraForm::Conical)
        {
            float TopZ = 0.f, TopWidth = 0.f, BaseWidth = 0.f;
            for (const FAhgMeshBuilder::FVert& V : B.Verts)
            {
                TopZ = FMath::Max(TopZ, V.P.Z);
            }
            for (const FAhgMeshBuilder::FVert& V : B.Verts)
            {
                const float W = FVector2f(V.P.X, V.P.Y).Size();
                (V.P.Z > TopZ * .9f ? TopWidth : BaseWidth) = FMath::Max(V.P.Z > TopZ * .9f ? TopWidth : BaseWidth, W);
            }
            TestTrue(*FString::Printf(TEXT("%s narrows to a point (triangular silhouette)"), S.Name), TopWidth < BaseWidth * .3f);
        }
      }
      TestTrue(*FString::Printf(TEXT("%s: organic only if rare"), S.Name), S.IsCommon() || S.Id == EFloraSpecies::Ancient);
      TestTrue(*FString::Printf(TEXT("%s: the variants differ"), S.Name), VertexCounts.Num() > 1);
    }
    // Distribution by biome and temperature.
    TMap<int32, int32> Forest, Beach, Cold;
    for (int32 I = 0; I < 20000; ++I)
    {
        const float Roll = (I + .5f) / 20000.f;
        Forest.FindOrAdd(FFloraRegistry::Pick(EAmeBiome::Forest, 14.f, Roll))++;
        Beach.FindOrAdd(FFloraRegistry::Pick(EAmeBiome::Beach, 20.f, Roll))++;
        Cold.FindOrAdd(FFloraRegistry::Pick(EAmeBiome::Mountain, 1.f, Roll))++;
    }
    TestTrue(TEXT("Beaches grow palms only"), Beach.Num() == 1 && Beach.Contains(static_cast<int32>(EFloraSpecies::Palm)));
    TestFalse(TEXT("No oaks on cold mountains"), Cold.Contains(static_cast<int32>(EFloraSpecies::Oak)));
    const float AncientShare = Forest.FindRef(static_cast<int32>(EFloraSpecies::Ancient)) / 20000.f;
    TestTrue(TEXT("Ancient trees are rare in forests (0 < share < 1%)"), AncientShare > 0.f && AncientShare < .01f);
    TestTrue(TEXT("Forests are mixed (4+ species)"), Forest.Num() >= 4);

    // Placement: deterministic, stable ids, ids decode back to their cell.
    const FAethraWorldGen& G = Gen();
    int32 Trees = 0, Mismatch = 0;
    TMap<int32, int32> Species;
    for (int32 Y = -60; Y < 60; ++Y)
    {
        for (int32 X = -60; X < 60; ++X)
        {
            FFloraTree A, B;
            const bool bA = FFloraPlacer::TreeAt(G, FIntPoint(X, Y), A), bB = FFloraPlacer::TreeAt(G, FIntPoint(X, Y), B);
            Mismatch += bA != bB || (bA && (A.Id != B.Id || A.Position != B.Position || A.Scale != B.Scale));
            if (bA)
            {
                ++Trees;
                Species.FindOrAdd(static_cast<int32>(A.Species))++;
                Mismatch += FFloraPlacer::CellOf(A.Id) != FIntPoint(X, Y);
                Mismatch += !G.Sample(A.Position).IsWater() ? 0 : 1;
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d trees of %d species in 840 x 840 m"), Trees, Species.Num()));
    TestEqual(TEXT("Placement deterministic, ids decode, never in water"), Mismatch, 0);
    TestTrue(TEXT("A forested landscape with several species"), Trees > 500 && Species.Num() >= 3);
    TestEqual(TEXT("Negative cells decode"), FFloraPlacer::CellOf(FFloraPlacer::MakeId(FIntPoint(-123456, 98765), EFloraSpecies::Pine)), FIntPoint(-123456, 98765));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW14, "Aethra.VisualWorld.14.FloraPersistence", AETHRA_VW_FLAGS)
bool FAethraVW14::RunTest(const FString&)
{
    CleanTestDir();
    const FString Pieces = FPaths::Combine(TestDir(), TEXT("Pieces.ahg"));
    uint64 Felled = 0;
    FIntPoint ChunkKey;
    int32 Before = 0;
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        // The first chunk near the origin with trees.
        TArray<FFloraTree> Trees;
        for (int32 K = 0; K < 16 && !Trees.Num(); ++K)
        {
            ChunkKey = FIntPoint(K % 4 - 2, K / 4 - 2);
            VW->TreesIn(0, ChunkKey, Trees);
        }
        if (!TestTrue(TEXT("A loaded chunk with trees"), Trees.Num() > 0 && VW->HasChunk(0, ChunkKey)))
        {
            return;
        }
        Before = Trees.Num();
        Felled = Trees[0].Id;
        TestTrue(TEXT("Felling saves"), VW->FellTree(Felled));
        TestFalse(TEXT("Felling twice does nothing"), VW->FellTree(Felled));
        TArray<FFloraTree> After;
        VW->TreesIn(0, ChunkKey, After);
        TestEqual(TEXT("One tree fewer in the chunk"), After.Num(), Before - 1);
        TestTrue(TEXT("Chunk rebuilt in place"), VW->HasChunk(0, ChunkKey));
        TestTrue(TEXT("Flora file written next to the pieces"), IFileManager::Get().FileExists(*VW->FloraPath));
    });
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        TestTrue(TEXT("Restart: the tree stays felled"), VW->FelledTrees().IsFelled(Felled));
        TArray<FFloraTree> After;
        VW->TreesIn(0, ChunkKey, After);
        TestEqual(TEXT("Restart: the chunk still lacks it"), After.Num(), Before - 1);
        TestFalse(TEXT("No tree with that id is generated"), After.ContainsByPredicate([Felled](const FFloraTree& T) { return T.Id == Felled; }));
    });
    // Another seed is unaffected.
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Other.ahg")), VWSeed + 7);
        TestEqual(TEXT("Another world has nothing felled"), VW->FelledTrees().Num(), 0);
    });
    CleanTestDir();
    return true;
}

// ================================================================ Prompt 08: FloraGen, fields, horizon,
// caves, ruins, materials and graphics profiles.

namespace
{
FFloraEnvironment EnvAtPoint(const FAethraWorldGen& G, const FVector2D& P)
{
    const float DX = G.Height(P + FVector2D(2, 0)) - G.Height(P - FVector2D(2, 0));
    const float DY = G.Height(P + FVector2D(0, 2)) - G.Height(P - FVector2D(0, 2));
    return FFloraEnvironment::From(G.Sample(P), FVector3f(-DX / 4.f, -DY / 4.f, 1.f).GetSafeNormal());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW15, "Aethra.VisualWorld.15.FloraGenEcology", AETHRA_VW_FLAGS)
bool FAethraVW15::RunTest(const FString&)
{
    const FAethraWorldGen& G = Gen();
    // Ground flora is deterministic and coherent with the place: alpine flowers only in the mountains,
    // ferns only in forests, nothing under water.
    int32 Plants = 0, Mismatch = 0, Misplaced = 0;
    TMap<int32, int32> Species;
    for (int32 Y = -150; Y < 150; Y += 3)
    {
        for (int32 X = -150; X < 150; X += 3)
        {
            const FIntPoint C(X * 7, Y * 7);
            const FVector2D P = FFloraGen::GroundPosition(VWSeed, C);
            const FFloraEnvironment Env = EnvAtPoint(G, P);
            FGroundPlant A, B;
            const bool bA = FFloraGen::GroundAt(VWSeed, C, Env, A), bB = FFloraGen::GroundAt(VWSeed, C, Env, B);
            Mismatch += bA != bB || (bA && (A.Species != B.Species || A.Position != B.Position));
            if (!bA)
            {
                continue;
            }
            ++Plants;
            Species.FindOrAdd(static_cast<int32>(A.Species))++;
            Misplaced += Env.bWater;
            Misplaced += A.Species == EGroundSpecies::AlpineFlower && Env.Biome != EAmeBiome::Mountain && Env.Biome != EAmeBiome::SnowPeak;
            Misplaced += A.Species == EGroundSpecies::Fern && Env.Biome != EAmeBiome::Forest;
        }
    }
    AddInfo(FString::Printf(TEXT("%d ground plants of %d species sampled over 6 x 6 km"), Plants, Species.Num()));
    TestEqual(TEXT("Ground flora deterministic"), Mismatch, 0);
    TestEqual(TEXT("Ground flora where its ecology allows"), Misplaced, 0);
    TestTrue(TEXT("Varied ground flora (6+ species)"), Species.Num() >= 6);
    // Ecology matters: the same biome with different moisture gives a different tree mix.
    FFloraEnvironment Wet, Dry;
    Wet.Biome = Dry.Biome = EAmeBiome::Plains;
    Wet.Temperature = Dry.Temperature = 16.f;
    Wet.Moisture = .9f;
    Dry.Moisture = .15f;
    TSet<int32> WetMix, DryMix;
    for (int32 I = 0; I < 400; ++I)
    {
        WetMix.Add(FFloraRegistry::PickAt(Wet, VWSeed, FVector2D(5e6, 5e6), (I + .5f) / 400.f));
        DryMix.Add(FFloraRegistry::PickAt(Dry, VWSeed, FVector2D(5e6, 5e6), (I + .5f) / 400.f));
    }
    TestTrue(TEXT("Dry plains grow dead trees, wet plains do not"), DryMix.Contains(static_cast<int32>(EFloraSpecies::DeadTree)) && !WetMix.Contains(static_cast<int32>(EFloraSpecies::DeadTree)));
    // Regional groves: the mix of a forest changes from one region to another.
    FFloraEnvironment Forest;
    Forest.Biome = EAmeBiome::Forest;
    Forest.Temperature = 14.f;
    float Birch[2] = { 0, 0 };
    for (int32 R = 0; R < 2; ++R)
    {
        float Best = R ? 1.f : 0.f;
        for (int32 K = 0; K < 40; ++K)
        {
            const FVector2D P(K * 900.0, 333.0);
            int32 Count = 0;
            for (int32 I = 0; I < 200; ++I)
            {
                Count += FFloraRegistry::PickAt(Forest, VWSeed, P, (I + .5f) / 200.f) == static_cast<int32>(EFloraSpecies::Birch);
            }
            Best = R ? FMath::Min(Best, Count / 200.f) : FMath::Max(Best, Count / 200.f);
        }
        Birch[R] = Best;
    }
    AddInfo(FString::Printf(TEXT("Birch share in forests: %.0f%% in the richest region, %.0f%% in the poorest"), Birch[0] * 100.f, Birch[1] * 100.f));
    TestTrue(TEXT("Groves: species shares vary by region"), Birch[0] > Birch[1] * 1.8f);
    // Resource contracts declared for future systems.
    TestTrue(TEXT("Sunflowers give seeds and dye"), EnumHasAllFlags(FFloraGen::Get(EGroundSpecies::Sunflower).Uses, EFloraUse::Seeds | EFloraUse::Dye));
    TestTrue(TEXT("Lavender gives perfume"), EnumHasAnyFlags(FFloraGen::Get(EGroundSpecies::Lavender).Uses, EFloraUse::Perfume));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW16, "Aethra.VisualWorld.16.FlowerFields", AETHRA_VW_FLAGS)
bool FAethraVW16::RunTest(const FString&)
{
    const FAethraWorldGen& G = Gen();
    // Fields are large regions in suitable places: count their area in a 6 x 6 km window (20 m grid)
    // and check each sample against the field's rules.
    TMap<int32, int32> Area;
    int32 Broken = 0, Samples = 0;
    for (int32 Y = -150; Y < 150; ++Y)
    {
        for (int32 X = -150; X < 150; ++X)
        {
            const FVector2D P(X * 20.0, Y * 20.0);
            const FFloraEnvironment Env = EnvAtPoint(G, P);
            const FFieldSample F = FFloraGen::FieldAt(VWSeed, P, Env);
            ++Samples;
            if (F.Strength <= .5f)
            {
                continue;
            }
            Area.FindOrAdd(static_cast<int32>(F.Field))++;
            Broken += Env.bWater;
            Broken += F.Field == EFlowerField::Sunflower && (Env.Slope > .14f || (Env.Biome != EAmeBiome::Plains && Env.Biome != EAmeBiome::Meadow));
            Broken += F.Field == EFlowerField::Alpine && Env.Biome != EAmeBiome::Mountain && Env.Biome != EAmeBiome::SnowPeak;
        }
    }
    FString Report;
    for (const TPair<int32, int32>& A : Area)
    {
        Report += FString::Printf(TEXT("%s %.0f ha; "), FFloraGen::FieldName(static_cast<EFlowerField>(A.Key)), A.Value * .04f);
    }
    AddInfo(TEXT("Field areas (6 x 6 km): ") + Report);
    TestEqual(TEXT("Every field sample follows its ecology"), Broken, 0);
    TestTrue(TEXT("Sunflower fields exist and are large (> 10 ha)"), Area.FindRef(static_cast<int32>(EFlowerField::Sunflower)) * .04f > 10.f);
    TestTrue(TEXT("At least 4 kinds of field"), Area.Num() >= 4);
    TestTrue(TEXT("Fields leave open land too (< 35% of the window)"), [&]() { int32 Sum = 0; for (const TPair<int32, int32>& A : Area) Sum += A.Value; return Sum < Samples * .35f; }());
    // Inside a strong sunflower field the plants are mostly sunflowers; trees keep out.
    FVector2D Inside = FVector2D::ZeroVector;
    for (int32 K = 0; K < 90000 && Inside.IsZero(); ++K)
    {
        const FVector2D P((K % 300 - 150) * 20.0, (K / 300 - 150) * 20.0);
        const FFieldSample F = FFloraGen::FieldAt(VWSeed, P, EnvAtPoint(G, P));
        if (F.Field == EFlowerField::Sunflower && F.Strength >= 1.f)
        {
            Inside = P;
        }
    }
    if (TestFalse(TEXT("A strong sunflower field found"), Inside.IsZero()))
    {
        int32 Sun = 0, All = 0, Trees = 0;
        const int32 CX = FMath::FloorToInt(Inside.X / FFloraGen::GroundCell), CY = FMath::FloorToInt(Inside.Y / FFloraGen::GroundCell);
        for (int32 J = -4; J <= 4; ++J)
        {
            for (int32 I = -4; I <= 4; ++I)
            {
                const FIntPoint C(CX + I, CY + J);
                FGroundPlant Plant;
                if (FFloraGen::GroundAt(VWSeed, C, EnvAtPoint(G, FFloraGen::GroundPosition(VWSeed, C)), Plant))
                {
                    ++All;
                    Sun += Plant.Species == EGroundSpecies::Sunflower;
                }
            }
        }
        for (int32 J = -2; J <= 2; ++J)
        {
            for (int32 I = -2; I <= 2; ++I)
            {
                FFloraTree T;
                Trees += FFloraPlacer::TreeAt(G, FIntPoint(FMath::FloorToInt(Inside.X / FFloraPlacer::Cell) + I, FMath::FloorToInt(Inside.Y / FFloraPlacer::Cell) + J), T);
            }
        }
        AddInfo(FString::Printf(TEXT("Field at (%.0f, %.0f): %d of %d plants are sunflowers, %d trees in 35 x 35 m"), Inside.X, Inside.Y, Sun, All, Trees));
        TestTrue(TEXT("A sunflower field is mostly sunflowers"), All > 20 && Sun > All * .6f);
        TestTrue(TEXT("Fields are open (at most 1 tree)"), Trees <= 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW17, "Aethra.VisualWorld.17.TreeLodAndVariants", AETHRA_VW_FLAGS)
bool FAethraVW17::RunTest(const FString&)
{
    CleanTestDir();
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        // The far level of a 256 m area keeps a subset of the trees of its four 128 m near chunks,
        // with the same ids (the same trees, fewer of them), only species marked for far.
        int32 Far = 0, Near = 0, Foreign = 0, NotFar = 0;
        TSet<uint64> NearIds;
        TSet<int32> Variants;
        for (int32 Y = 4; Y < 8; ++Y)
        {
            for (int32 X = 4; X < 8; ++X)
            {
                TArray<FFloraTree> T;
                VW->TreesIn(0, FIntPoint(X, Y), T);
                Near += T.Num();
                for (const FFloraTree& Tree : T)
                {
                    NearIds.Add(Tree.Id);
                    Variants.Add(Tree.Variant);
                }
            }
        }
        for (int32 Y = 2; Y < 4; ++Y)
        {
            for (int32 X = 2; X < 4; ++X)
            {
                TArray<FFloraTree> T;
                VW->TreesIn(1, FIntPoint(X, Y), T);
                Far += T.Num();
                for (const FFloraTree& Tree : T)
                {
                    Foreign += !NearIds.Contains(Tree.Id);
                    NotFar += !FFloraRegistry::Get(Tree.Species).bFar;
                }
            }
        }
        AddInfo(FString::Printf(TEXT("512 x 512 m: %d trees near, %d far (%.0f%%), %d variants used"), Near, Far, Near ? 100.f * Far / Near : 0.f, Variants.Num()));
        TestTrue(TEXT("Trees present"), Near > 100);
        TestEqual(TEXT("Far trees are a subset of the near trees"), Foreign, 0);
        TestEqual(TEXT("Far chunks only keep far species"), NotFar, 0);
        TestTrue(TEXT("Far keeps about 40%"), Far > Near * .25f && Far < Near * .55f);
        TestEqual(TEXT("All three variants used"), Variants.Num(), FFloraRegistry::Variants);
        // Profiles scale draw distances only: the trees themselves are the same.
        TArray<FFloraTree> High, Low;
        VW->TreesIn(0, FIntPoint(5, 5), High);
        VW->SetQuality(0);
        VW->TreesIn(0, FIntPoint(5, 5), Low);
        TestEqual(TEXT("Low profile: the same trees"), Low.Num(), High.Num());
        TestTrue(TEXT("Low profile: shorter tree distance"), AAethraVisualWorld::ProfileFor(0).TreeDistance < AAethraVisualWorld::ProfileFor(2).TreeDistance);
    });
    CleanTestDir();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW18, "Aethra.VisualWorld.18.ReachableHorizon", AETHRA_VW_FLAGS)
bool FAethraVW18::RunTest(const FString&)
{
    // Aethra Reachable Horizon Rule: the horizon is the same WorldGen surface, 10 m lower and coarser,
    // never an invented landscape. Every horizon vertex must match the real ground under it.
    CleanTestDir();
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->FlushHorizon();
        const FVwProfile& Q = VW->Profile();
        AddInfo(FString::Printf(TEXT("Profile %s: %d horizon tiles, reach %.1f km (radius %.0f km)"), Q.Name, VW->HorizonTileCount(), VW->HorizonReachM() / 1000.0, Q.HorizonKm));
        TestTrue(TEXT("The horizon reaches the profile's radius"), VW->HorizonReachM() >= Q.HorizonKm * 1000.0);
        const FAethraWorldGen& G = VW->Gen();
        int32 Checked = 0, Wrong = 0;
        double Worst = 0;
        for (const FIntPoint Tile : { FIntPoint(0, 0), FIntPoint(-1, 0), FIntPoint(2, -3), FIntPoint(-4, 3) })
        {
            const UProceduralMeshComponent* Mesh = VW->HorizonMesh(Tile);
            if (!TestNotNull(*FString::Printf(TEXT("Tile (%d, %d) built"), Tile.X, Tile.Y), Mesh))
            {
                continue;
            }
            const FProcMeshSection* S = const_cast<UProceduralMeshComponent*>(Mesh)->GetProcMeshSection(0);
            for (int32 I = 0; I < S->ProcVertexBuffer.Num(); I += 37)
            {
                const FVector V = S->ProcVertexBuffer[I].Position;
                const double Real = G.Height(FVector2D(V.X / 100.0, V.Y / 100.0)) - 10.0;
                const double Error = V.Z / 100.0 - Real;
                if (Error > -1.0) // skirt vertices hang 4 m lower by design
                {
                    ++Checked;
                    Worst = FMath::Max(Worst, FMath::Abs(Error));
                    Wrong += FMath::Abs(Error) > .01;
                }
            }
        }
        AddInfo(FString::Printf(TEXT("%d horizon vertices checked against the real ground, worst %.3f m"), Checked, Worst));
        TestTrue(TEXT("Vertices checked"), Checked > 200);
        TestEqual(TEXT("The horizon shows the real ground (no invented relief)"), Wrong, 0);
        // The real terrain near the viewer matches the horizon's geography: same heights at shared points.
        const UProceduralMeshComponent* Near = VW->ChunkMesh(1, FIntPoint(2, 2));
        if (Near)
        {
            const FProcMeshSection* S = const_cast<UProceduralMeshComponent*>(Near)->GetProcMeshSection(0);
            int32 Far = 0;
            for (int32 I = 0; I < S->ProcVertexBuffer.Num(); I += 53)
            {
                const FVector V = S->ProcVertexBuffer[I].Position;
                // Far chunks sit 0.4 m low (and skirts hang 4 m lower still).
                const double Off = V.Z / 100.0 - G.Height(FVector2D(V.X / 100.0, V.Y / 100.0));
                Far += FMath::Abs(Off + .4) > .01 && FMath::Abs(Off + 4.4) > .01;
            }
            TestEqual(TEXT("Streamed chunks use the same surface"), Far, 0);
        }
        // Ruins and the settlement stand on that ground, so the horizon shows where they are.
        int32 Floating = 0;
        for (const FRuinSite& R : VW->RuinGen()->Sites())
        {
            Floating += !R.bUnderground && FMath::Abs(R.Anchor.Z - G.Height(FVector2D(R.Anchor))) > .01;
        }
        TestEqual(TEXT("Surface ruins stand on the real ground"), Floating, 0);
    });
    CleanTestDir();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW19, "Aethra.VisualWorld.19.CaveGeneration", AETHRA_VW_FLAGS)
bool FAethraVW19::RunTest(const FString&)
{
    const FAethraWorldGen& G = Gen();
    const FVector2D Spawn(0, 0);
    FAethraCaveGen A(G, Spawn), B(G, Spawn);
    TestTrue(TEXT("Caves planned"), A.Systems().Num() >= 4);
    TestEqual(TEXT("Deterministic plan"), A.Systems().Num(), B.Systems().Num());
    TSet<int32> Kinds;
    for (const FCaveSystem& S : A.Systems())
    {
        Kinds.Add(static_cast<int32>(S.Kind));
    }
    AddInfo(FString::Printf(TEXT("%d cave systems of %d kinds, %d volume blocks"), A.Systems().Num(), Kinds.Num(), A.ActiveBlocks().Num()));
    TestTrue(TEXT("At least 3 kinds of cave"), Kinds.Num() >= 3);
    const FCaveSystem* Demo = A.DemoSystem();
    if (!TestNotNull(TEXT("Demo cave"), Demo))
    {
        return false;
    }
    TestTrue(TEXT("Demo cave near the spawn"), FVector2D(Demo->Entrance).Size() < 800.0);
    TestTrue(TEXT("Entrance on the surface"), FMath::Abs(Demo->Entrance.Z - G.Height(FVector2D(Demo->Entrance))) < .01);
    // Reachable: air all the way along the tunnels, from outside to the deepest chamber.
    int32 Solid = 0, Steps = 0;
    for (const FCaveTunnel& T : Demo->Tunnels)
    {
        for (float U = 0.f; U <= 1.f; U += .02f)
        {
            ++Steps;
            Solid += !A.IsAir(FMath::Lerp(T.A, T.B, U));
        }
    }
    TestEqual(TEXT("The tunnels are open from the entrance to the chambers"), Solid, 0);
    const FCaveChamber& Deep = Demo->Chambers.Last();
    TestTrue(TEXT("The deepest chamber is underground (> 8 m of rock above)"), G.Height(FVector2D(Deep.Center)) - (Deep.Center.Z + Deep.Radii.Z) > 3.f && G.Height(FVector2D(Deep.Center)) - Deep.Center.Z > 8.f);
    // Same density from two generators; rock below the ground, air above it.
    FRandomStream R(19);
    int32 Diff = 0, Wrong = 0;
    for (int32 I = 0; I < 2000; ++I)
    {
        const FVector P(R.FRandRange(-800.f, 800.f), R.FRandRange(-800.f, 800.f), 0.f);
        const float H = G.Height(FVector2D(P));
        const FVector Q(P.X, P.Y, H + R.FRandRange(-40.f, 40.f));
        Diff += A.Density(Q) != B.Density(Q);
        if (!A.Systems().ContainsByPredicate([&Q](const FCaveSystem& S) { return S.Bounds.ExpandBy(4.0).IsInside(Q); }))
        {
            if (FMath::Abs(Q.Z - H) > .5f) // the volume samples the ground on a 1 m lattice
            {
                Wrong += (Q.Z < H) != !A.IsAir(Q);
            }
        }
    }
    TestEqual(TEXT("Density deterministic"), Diff, 0);
    TestEqual(TEXT("Outside caves: rock below the surface, air above"), Wrong, 0);
    // The entrance block meshes into stylised rock and the height field opens over the cave.
    FCaveMesh M;
    A.MeshBlock(FAethraCaveGen::BlockOf(Demo->Entrance - FVector(0, 0, 2.0)), M);
    AddInfo(FString::Printf(TEXT("Entrance block: %d triangles"), M.Triangles.Num() / 3));
    TestTrue(TEXT("Entrance block has geometry"), M.Triangles.Num() > 300);
    TestTrue(TEXT("The height field opens over the cave mouth"), A.CoversColumn(FVector2D(Demo->Entrance), 2.0));
    TArray<FAethraCaveGen::FFormation> F;
    A.Formations(*Demo, F);
    int32 Crystals = 0;
    for (const FAethraCaveGen::FFormation& X : F)
    {
        Crystals += X.Type == 2;
    }
    AddInfo(FString::Printf(TEXT("Demo cave (%s): %d formations, %d crystals"), FAethraCaveGen::KindName(Demo->Kind), F.Num(), Crystals));
    TestTrue(TEXT("Stalactites and stalagmites"), F.Num() > 10);
    TestTrue(TEXT("The enchanted demo cave has Aetherite crystals"), Demo->Kind != ECaveKind::Enchanted || Crystals > 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW20, "Aethra.VisualWorld.20.ExcavationPersistence", AETHRA_VW_FLAGS)
bool FAethraVW20::RunTest(const FString&)
{
    CleanTestDir();
    const FString Pieces = FPaths::Combine(TestDir(), TEXT("Pieces.ahg"));
    FVector Hole = FVector::ZeroVector;
    int32 TrianglesBefore = 0, TrianglesAfter = 0;
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        // Dig on open ground, away from caves: a new volume region opens there.
        const FVector2D At(-60.0, 40.0);
        Hole = FVector(At.X, At.Y, VW->Gen().Height(At) - .5f);
        TestFalse(TEXT("Rock before digging"), VW->CaveGen()->IsAir(Hole - FVector(0, 0, .8f)));
        TestTrue(TEXT("Dig saved"), VW->Dig(Hole * 100.0, 2.5f));
        TestTrue(TEXT("Air after digging"), VW->CaveGen()->IsAir(Hole - FVector(0, 0, .8f)));
        const UProceduralMeshComponent* Block = VW->CaveBlockMesh(FAethraCaveGen::BlockOf(Hole));
        TestNotNull(TEXT("The dug block is meshed"), Block);
        TestTrue(TEXT("The height field opened there"), VW->CaveGen()->CoversColumn(At, 2.0));
        TestTrue(TEXT("Edits file written"), IFileManager::Get().FileExists(*VW->CavePath));
        // Filling puts rock back.
        TestTrue(TEXT("Fill saved"), VW->Dig((Hole + FVector(0, 0, -1.f)) * 100.0, 1.f, true));
        TestFalse(TEXT("Rock after filling"), VW->CaveGen()->IsAir(Hole - FVector(0, 0, 1.f)));
    });
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        TestEqual(TEXT("Restart: both edits restored"), VW->CaveGen()->Edits().Num(), 2);
        TestTrue(TEXT("Restart: the hole is still there"), VW->CaveGen()->IsAir(Hole + FVector(-1.6f, 0, -.5f)) && VW->CaveGen()->IsAir(Hole + FVector(1.6f, 0, -.5f)));
        TestFalse(TEXT("Restart: the filled part is still rock"), VW->CaveGen()->IsAir(Hole - FVector(0, 0, 1.f)));
    });
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Other.ahg")), VWSeed + 7);
        TestEqual(TEXT("Another world has no edits"), VW->CaveGen()->Edits().Num(), 0);
    });
    CleanTestDir();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW21, "Aethra.VisualWorld.21.RuinGeneration", AETHRA_VW_FLAGS)
bool FAethraVW21::RunTest(const FString&)
{
    const FAethraWorldGen& G = Gen();
    const FAethraCaveGen Caves(G, FVector2D::ZeroVector);
    const FAethraRuinGen A(G, &Caves, FVector2D::ZeroVector), B(G, &Caves, FVector2D::ZeroVector);
    TestEqual(TEXT("Deterministic"), A.Sites().Num(), B.Sites().Num());
    TSet<int32> Kinds, Cultures;
    int32 Surface = 0, Underground = 0, InWater = 0, BadCave = 0, NoLore = 0;
    for (const FRuinSite& S : A.Sites())
    {
        Kinds.Add(static_cast<int32>(S.Kind));
        Cultures.Add(static_cast<int32>(S.Culture));
        NoLore += S.Lore.Num() < 3;
        if (S.bUnderground)
        {
            ++Underground;
            BadCave += !Caves.IsAir(S.Anchor + FVector(0, 0, 1.f)) || S.CaveSystem < 0;
        }
        else
        {
            ++Surface;
            InWater += G.Sample(FVector2D(S.Anchor)).IsWater();
        }
    }
    AddInfo(FString::Printf(TEXT("%d ruins (%d surface, %d underground), %d kinds, %d cultures"), A.Sites().Num(), Surface, Underground, Kinds.Num(), Cultures.Num()));
    TestTrue(TEXT("Surface and underground ruins"), Surface > 5 && Underground > 1);
    TestTrue(TEXT("Varied kinds (5+)"), Kinds.Num() >= 5);
    TestTrue(TEXT("Several ancient cultures"), Cultures.Num() >= 2);
    TestEqual(TEXT("No ruin in water"), InWater, 0);
    TestEqual(TEXT("Underground ruins stand in cave air"), BadCave, 0);
    TestEqual(TEXT("Every ruin has lore tags"), NoLore, 0);
    const FRuinSite* Temple = A.Sites().FindByPredicate([](const FRuinSite& S) { return S.Kind == ERuinKind::Temple && !S.bUnderground; });
    if (TestNotNull(TEXT("A temple"), Temple))
    {
        FRuinSite Ruined = *Temple, Intact = *Temple;
        Ruined.Preservation = .05f;
        Intact.Preservation = .95f;
        TArray<FRuinPiece> PiecesRuined, PiecesIntact, P1, P2;
        FAethraRuinGen::Build(Ruined, VWSeed, PiecesRuined);
        FAethraRuinGen::Build(Intact, VWSeed, PiecesIntact);
        FAethraRuinGen::Build(*Temple, VWSeed, P1);
        FAethraRuinGen::Build(*Temple, VWSeed, P2);
        int32 Upright[2] = { 0, 0 };
        for (int32 K = 0; K < 2; ++K)
        {
            for (const FRuinPiece& P : K ? PiecesIntact : PiecesRuined)
            {
                Upright[K] += P.Shape == EAhgShape::Column && FMath::Abs(P.Transform.GetRotation().Rotator().Pitch) < 30.f && P.Transform.GetScale3D().Z > 4.f;
            }
        }
        AddInfo(FString::Printf(TEXT("Temple: %d pieces; whole columns %d when ruined, %d when well kept"), P1.Num(), Upright[0], Upright[1]));
        TestTrue(TEXT("Same site, same pieces"), P1.Num() == P2.Num() && P1.Num() > 30);
        TestTrue(TEXT("Preservation decides how much still stands"), Upright[1] > Upright[0] + 3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW22, "Aethra.VisualWorld.22.MaterialIntegrity", AETHRA_VW_FLAGS)
bool FAethraVW22::RunTest(const FString&)
{
    // No plasticine: natural surfaces are rough, never metallic, never glowing (except the enchanted
    // ones), and every surface has some procedural relief.
    int32 Plastic = 0, Smooth = 0, Glowing = 0;
    for (const FAmeMaterialDef& D : FAmeRegistry::All())
    {
        const bool bNatural = D.Category == EAmeCategory::Terrain || D.Category == EAmeCategory::Rock || D.Category == EAmeCategory::Vegetation;
        if (bNatural && (D.Roughness < .34f || D.Metallic > 0.f))
        {
            AddError(FString::Printf(TEXT("%s looks plastic (roughness %.2f, metallic %.2f)"), D.Name, D.Roughness, D.Metallic));
            ++Plastic;
        }
        Smooth += FAmeRegistry::BumpCm(D) <= 0.f;
        Glowing += D.Glow > 0.f && D.Id != EAmeSurface::Aetherite && D.Id != EAmeSurface::GlowMoss;
        TestTrue(*FString::Printf(TEXT("%s has colour variation"), D.Name), D.Variation > 0.f || D.Base != D.Accent);
    }
    TestEqual(TEXT("No plastic-looking natural surface"), Plastic, 0);
    TestEqual(TEXT("Every surface has relief"), Smooth, 0);
    TestEqual(TEXT("Only enchanted surfaces glow"), Glowing, 0);
    TestTrue(TEXT("The palette fits 64 surfaces"), FAmeRegistry::All().Num() <= FAmeRegistry::PaletteWidth);
    // Every flora mesh uses AME surfaces only (one material, shared textures: no per-plant textures).
    int32 Bad = 0;
    for (const FGroundSpecies& S : FFloraGen::Ground())
    {
        FAhgMeshBuilder B;
        FFloraGen::BuildMesh(S, VWSeed, B);
        for (const FAhgMeshBuilder::FVert& V : B.Verts)
        {
            Bad += V.Surface < 0.f || V.Surface >= static_cast<float>(EAmeSurface::Count);
        }
        TestTrue(*FString::Printf(TEXT("%s has geometry"), S.Name), B.Verts.Num() > 0);
    }
    TestEqual(TEXT("Flora uses AME surfaces"), Bad, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW23, "Aethra.VisualWorld.23.GraphicsProfiles", AETHRA_VW_FLAGS)
bool FAethraVW23::RunTest(const FString&)
{
    for (int32 Q = 1; Q < 4; ++Q)
    {
        const FVwProfile& A = AAethraVisualWorld::ProfileFor(Q - 1), &B = AAethraVisualWorld::ProfileFor(Q);
        TestTrue(*FString::Printf(TEXT("%s draws no less than %s"), B.Name, A.Name), B.NearRadius >= A.NearRadius && B.FarRadius >= A.FarRadius && B.HorizonKm >= A.HorizonKm
            && B.GroundDensity >= A.GroundDensity && B.ShadowDistance >= A.ShadowDistance);
    }
    CleanTestDir();
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->SetQuality(0);
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        const int32 LowChunks = VW->ChunkCount(0) + VW->ChunkCount(1);
        const int32 LowRuins = VW->RuinPieceCount();
        TArray<FFloraTree> LowTrees;
        VW->TreesIn(0, FIntPoint(1, 1), LowTrees);
        VW->SetQuality(3);
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        const int32 UltraChunks = VW->ChunkCount(0) + VW->ChunkCount(1);
        TArray<FFloraTree> UltraTrees;
        VW->TreesIn(0, FIntPoint(1, 1), UltraTrees);
        AddInfo(FString::Printf(TEXT("Chunks: Low %d, Ultra %d"), LowChunks, UltraChunks));
        TestTrue(TEXT("Low draws less"), LowChunks < UltraChunks);
        TestEqual(TEXT("Same world: same trees"), LowTrees.Num(), UltraTrees.Num());
        TestEqual(TEXT("Same world: same ruins"), VW->RuinPieceCount(), LowRuins);
        TestEqual(TEXT("Same world: same caves"), VW->CaveGen()->Systems().Num(), FAethraCaveGen(VW->Gen(), VW->bHasSettlement ? VW->SettlementCenter : FVector2D::ZeroVector).Systems().Num());
    });
    CleanTestDir();
    return true;
}

#endif
